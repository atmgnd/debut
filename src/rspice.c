/*
   Coryright (C) 2019 atmgnd@outlook.com

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>

#define _KLIB_IMPL_
#include "rspice.h"

#ifdef G_OS_WIN32
#include <Windows.h>
#include <initguid.h>
#include <usbiodef.h>
#include <Dbt.h>
#endif
// #include <gst/gst.h>

static GMainLoop *mainloop = NULL; // TODO run mainloop in a seperate thread forever, until program terminate

typedef struct _rspice_display_area
{
    int x;
    int y;
    int w;
    int h;
} rspice_display_area;

KDQ_INIT(rspice_display_area)

struct spice_connection
{
    int connections;
    SpiceSession *session;
    SpiceMainChannel *main;
    SpiceInputsChannel *inputs;

    enum SpiceSurfaceFmt d_format;
    gint d_width, d_height, d_stride;
    gpointer d_data;

    kdq_t(rspice_display_area) * pixel_queue[2];
    gpointer pixel_data[2];
    pthread_mutex_t pixel_lock;
    volatile int pixel_painting;

    rspice_mouse_info mouse;
    pthread_mutex_t mouse_lock;

    SpiceUsbDeviceManager *usb_manager;
    kdq_t(rspice_usb_device) * usb_list[2];
    int usb_sigal_connected;
    pthread_mutex_t usb_lock;

    int last_err;
};

#define RSPICE_FREE_AND_RESET(x, f) do{if(x) f(x);x=NULL;}while(0)

void rspice_usblist_free(SpicyConnection *con);
/* ------------------------------------------------------------------ */

static void lock_init(pthread_mutex_t *l)
{
    pthread_mutexattr_t attr;

    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(l, &attr);
    pthread_mutexattr_destroy(&attr);
}

static void primary_create(SpiceChannel *channel, gint format,
                           gint width, gint height, gint stride,
                           gint shmid, gpointer imgdata, gpointer data)
{
    struct spice_connection *c = data;
    SPICE_DEBUG("%s: %dx%d, format %d", __FUNCTION__, width, height, format);
    pthread_mutex_lock(&c->pixel_lock);
    while (c->pixel_painting) { } /* protect c->d_data from read access, pixel_painting like a dummy spin lock, but we do only spin on one side, do it more elegant way ? */
    c->d_format = format;
    c->d_width = width;
    c->d_height = height;
    c->d_stride = stride;
    c->d_data = imgdata; /* we assume the previous d_data is valid until callback here complete */
    c->pixel_queue[1]->count = 0;
    RSPICE_FREE_AND_RESET(c->pixel_data[1], free);
    c->pixel_data[1] = malloc(sizeof(struct _rspice_display_pixel) + width * height * 4); // We do only 1 monitor
    memset(c->pixel_data[1], 0xff, width * height * 4);
    pthread_mutex_unlock(&c->pixel_lock);
}

#define _RSPICE_MAX_ARA (16)
static void invalidate(SpiceChannel *channel,
                       gint x, gint y, gint w, gint h, gpointer data)
{
    struct spice_connection *c = data;

    switch (c->d_format)
    {
    case SPICE_SURFACE_FMT_32_xRGB:
        pthread_mutex_lock(&c->pixel_lock);
        if (kdq_size(c->pixel_queue[1]) < _RSPICE_MAX_ARA) /* here the count is _RSPICE_MAX_ARA will causes a full update */
        {
            rspice_display_area ara = {x, y, w, h};
            kdq_push(rspice_display_area, c->pixel_queue[1], ara);
        }
        pthread_mutex_unlock(&c->pixel_lock);
        break;
    default:
        SPICE_DEBUG("unsupported spice surface format %u\n", c->d_format);
        // g_main_loop_quit(mainloop);
        break;
    }
}

static void main_channel_event(SpiceChannel *channel, SpiceChannelEvent event,
                               gpointer data)
{
    struct spice_connection *c = data;

    switch (event)
    { // TODO handleing other message
    case SPICE_CHANNEL_OPENED:
        break;
    case SPICE_CHANNEL_SWITCHING:
        break;
    case SPICE_CHANNEL_CLOSED: // TODO when this message ?
    case SPICE_CHANNEL_ERROR_CONNECT:
    case SPICE_CHANNEL_ERROR_TLS:
    case SPICE_CHANNEL_ERROR_LINK:
    case SPICE_CHANNEL_ERROR_AUTH:
    case SPICE_CHANNEL_ERROR_IO:
    {
        const GError *error = spice_channel_get_error(channel);
        if (error)
            SPICE_DEBUG("channel error: %d - %s", event, error->message);

        spice_session_disconnect(c->session);
    }
    break;
    default:
        g_warning("main channel event: %u", event);
        break;
    }
}

static void main_mouse_update(SpiceChannel *channel, gpointer data)
{
    struct spice_connection *conn = data;
    gint mode;

    g_object_get(channel, "mouse-mode", &mode, NULL);
    switch (mode)
    {
    case SPICE_MOUSE_MODE_SERVER:
        conn->mouse.mouse_state = sms_server;
        break;
    case SPICE_MOUSE_MODE_CLIENT:
        conn->mouse.mouse_state = sms_client;
        break;
    default:
        conn->mouse.mouse_state = sms_unknow;
        break;
    }
}

static void cursor_set(SpiceCursorChannel *channel,
                       G_GNUC_UNUSED GParamSpec *pspec,
                       gpointer data)
{
    struct spice_connection *conn = data;
    SpiceCursorShape *cursor_shape = NULL;
    SDL_Surface *surf = NULL;

    do
    {
        conn->mouse.hide_mouse = 0;
        g_object_get(G_OBJECT(channel), "cursor", &cursor_shape, NULL);
        if (G_UNLIKELY(cursor_shape == NULL || cursor_shape->data == NULL))
            break;

        /* SDL_LIL_ENDIAN only */
        surf = SDL_CreateRGBSurfaceFrom((void *)cursor_shape->data, cursor_shape->width, cursor_shape->height,
                                                     32, 4 * cursor_shape->width, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
        if (surf == NULL)
            break;

        SDL_Cursor *cursor = SDL_CreateColorCursor(surf, cursor_shape->hot_spot_x, cursor_shape->hot_spot_y);
        if (!cursor)
            break;

        pthread_mutex_lock(&conn->mouse_lock);
        conn->mouse.cursor_update_state[1] = 1;
        conn->mouse.sdl_cursor[1] = cursor;
        pthread_mutex_unlock(&conn->mouse_lock);
    } while (0);

    if (cursor_shape != NULL)
        g_boxed_free(SPICE_TYPE_CURSOR_SHAPE, cursor_shape);

    if (surf)
        SDL_FreeSurface(surf);
}

static void cursor_move(SpiceCursorChannel *channel, gint x, gint y, gpointer data)
{
    struct spice_connection *conn = data;
    conn->mouse.hide_mouse = 0;

    // we do nothing, we send client mouse position every frame
}

static void cursor_hide(SpiceCursorChannel *channel, gpointer data)
{
    struct spice_connection *conn = data;

    conn->mouse.hide_mouse = 1;
}

static void cursor_reset(SpiceCursorChannel *channel, gpointer data)
{
    struct spice_connection *conn = data;

    pthread_mutex_lock(&conn->mouse_lock);
    conn->mouse.cursor_update_state[1] = 1;
    pthread_mutex_unlock(&conn->mouse_lock);
}

static void channel_new(SpiceSession *s, SpiceChannel *channel, gpointer data)
{
    struct spice_connection *c = data;
    int id;

    g_object_get(channel, "channel-id", &id, NULL);
    if (SPICE_IS_MAIN_CHANNEL(channel))
    {
        c->main = SPICE_MAIN_CHANNEL(channel);
        g_signal_connect(channel, "channel-event",
                         G_CALLBACK(main_channel_event), data);

        g_signal_connect(channel, "main-mouse-update",
                         G_CALLBACK(main_mouse_update), data);
        main_mouse_update(channel, data);
    }

    if (SPICE_IS_DISPLAY_CHANNEL(channel))
    {
        if (id != 0)
            return;

        g_signal_connect(channel, "display-primary-create",
                         G_CALLBACK(primary_create), data);
        g_signal_connect(channel, "display-invalidate",
                         G_CALLBACK(invalidate), data);
        spice_channel_connect(channel);
        // spice_display_channel_change_preferred_video_codec_type(channel, SPICE_VIDEO_CODEC_TYPE_H264); // need recompile spice server ?
    }

    if (SPICE_IS_INPUTS_CHANNEL(channel))
    {
        c->inputs = SPICE_INPUTS_CHANNEL(channel);

        spice_channel_connect(channel);
        //         spice_g_signal_connect_object(channel, "channel-event",
        //             G_CALLBACK(inputs_channel_event), display, 0); // TODO in this rspice set keypress_delay, is this important ?
    }

    if (SPICE_IS_CURSOR_CHANNEL(channel))
    {
        if (id != 0)
            return;

        g_signal_connect(channel, "notify::cursor",
                         G_CALLBACK(cursor_set), data);
        g_signal_connect(channel, "cursor-move",
                         G_CALLBACK(cursor_move), data);
        g_signal_connect(channel, "cursor-hide",
                         G_CALLBACK(cursor_hide), data);
        g_signal_connect(channel, "cursor-reset",
                         G_CALLBACK(cursor_reset), data);
        spice_channel_connect(channel);
        return;
    }
}

static void connection_destroy(SpiceSession *session,
                               struct spice_connection *conn)
{
    g_object_unref(conn->session);

    SPICE_DEBUG("connection destro");
    if (--conn->connections > 0)
    {
        return;
    }

    g_main_loop_quit(mainloop);
}
/* ------------------------------------------------------------------ */

SpicyConnection *rspice_new_connection()
{
    struct spice_connection *p = (struct spice_connection *)malloc(sizeof(struct spice_connection));

    memset(p, 0, sizeof(struct spice_connection));
    p->pixel_queue[0] = kdq_init(rspice_display_area);
    p->pixel_queue[1] = kdq_init(rspice_display_area);
    lock_init(&p->pixel_lock);
    lock_init(&p->mouse_lock);
    lock_init(&p->usb_lock);

    return (SpicyConnection *)p;
}

static char cached_args[256] = "rspice";
void rspice_cache_args(const char *args)
{
    strcat(cached_args, " " );
    strncat(cached_args, args, 224 );
}

#define DUMMYSH_TOK_BUFSIZE 64
#define DUMMYSH_TOK_DELIM " \t\r\n\a"
static char **dummy_gen_args(char *line, int *pargc)
{
    int bufsize = DUMMYSH_TOK_BUFSIZE, position = 0;
    char **tokens = (char **)malloc(bufsize * sizeof(char *));
    char *token, *saveptr, **tokens_backup;

    if (!tokens)
    {
        return NULL;
    }

    token = strtok_r(line, DUMMYSH_TOK_DELIM, &saveptr);
    while (token != NULL)
    {
        tokens[position] = token;
        position++;

        if (position >= bufsize)
        {
            bufsize += DUMMYSH_TOK_BUFSIZE;
            tokens_backup = tokens;
            tokens = (char **)realloc(tokens, bufsize * sizeof(char *));
            if (!tokens)
            {
                free(tokens_backup);
                return NULL;
            }
        }

        token = strtok_r(NULL, DUMMYSH_TOK_DELIM, &saveptr);
    }
    tokens[position] = NULL;

    *pargc = position;
    return tokens;
}

#ifdef G_OS_WIN32
static void *winusb_thread_rt(void *arg)
{
    int *err_out = (int *)arg;
    GError *err = NULL;
    SpiceSession *session = spice_session_new();

    SpiceUsbDeviceManager *um = spice_usb_device_manager_get(session, &err);
    if (err || um == NULL)
    {
        g_clear_error(&err);
        *err_out = -1;
        g_object_unref(session);
        return NULL;
    }

    *err_out = 1;
    do
    {
        MSG msg;
        while (TRUE)
        {
            BOOL bRet = GetMessage(&msg, NULL, 0, 0);
            if ((bRet == 0) || (bRet == -1))
            {
                break;
            }

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    } while (0);

    g_object_unref(session);

    return NULL;
}

static pthread_once_t usb_rt_once = PTHREAD_ONCE_INIT;
static void rspice_usb_rt(void)
{
    pthread_t winusb_thread;
    int err = 0;
    pthread_create(&winusb_thread, NULL, winusb_thread_rt, &err);

    while (!err)
        ;
    if (err != 1)
    {
    }
}
#endif

static pthread_once_t delay_main_once = PTHREAD_ONCE_INIT;
static void rspice_delay_main(void)
{
    int argc = 0;
    char **argv = NULL;
    char *args_str = strdup(cached_args ); // --spice-debug --spice-disable-audio
    argv = dummy_gen_args(args_str, &argc);

    GError *error = NULL;
    GOptionContext *context;

    //     gst_init(&argc, &argv);
    context = g_option_context_new("- spice client test application");
    g_option_context_set_summary(context, "Imgui test client to connect to Spice servers");
    g_option_context_set_description(context, "Report bugs to atmgnd@outlook.com");
    g_option_context_add_group(context, spice_get_option_group());
    //     g_option_context_add_group(context, gst_init_get_option_group());
    if (!g_option_context_parse(context, &argc, &argv, &error))
    {
        g_print("option parsing failed: %s\n", error->message);
        exit(1);
    }
    g_option_context_free(context);
    free(args_str);
    free(argv);
}

int rspice_main(const char *host, const char *port, SpicyConnection *con)
{
    pthread_once(&delay_main_once, rspice_delay_main);

#ifdef G_OS_WIN32
    pthread_once(&usb_rt_once, rspice_usb_rt);
#endif

    struct spice_connection *connection = (struct spice_connection *)con;

    // TODO init gst, deinit gst, in real main
    mainloop = g_main_loop_new(NULL, false);

    SpiceSession *session = spice_session_new();
    connection->session = session;
    connection->connections++;
    // TODO handle other event
    g_signal_connect(session, "channel-new",
                     G_CALLBACK(channel_new), connection);
    g_signal_connect(session, "disconnected",
                     G_CALLBACK(connection_destroy), connection);

    spice_set_session_option(session);

    // setup host url ...
    g_object_set(session, "host", host, NULL);
    g_object_set(session, "port", port, NULL);

    if (!spice_session_connect(session))
    { // ?
        g_main_loop_unref(mainloop);
        g_object_unref(session);
        return -1;
    }

    g_main_loop_run(mainloop);
    g_main_loop_unref(mainloop);

    return 0;
}

void rspice_terminate(SpicyConnection *con)
{
    spice_session_disconnect(((struct spice_connection *)con)->session);
}

static void device_destroy_list(kdq_t(rspice_usb_device) * usb_list)
{
    for (size_t i = 0; i < kdq_size(usb_list); i++)
    {
        rspice_usb_device *_d = &(kdq_at(usb_list, i));
        g_boxed_free(spice_usb_device_get_type(), _d->device);
    }
    kdq_destroy(rspice_usb_device, usb_list);
}

int rspice_free(SpicyConnection *con)
{
    struct spice_connection *p = (struct spice_connection *)con;

    rspice_usblist_free(con);
    pthread_mutex_destroy(&p->pixel_lock);
    pthread_mutex_destroy(&p->mouse_lock);
    pthread_mutex_destroy(&p->usb_lock);
    RSPICE_FREE_AND_RESET(p->pixel_data[0], free);
    RSPICE_FREE_AND_RESET(p->pixel_data[1], free);
    if(p->mouse.sdl_cursor[0]) SDL_FreeCursor(p->mouse.sdl_cursor[0]);
    if(p->mouse.sdl_cursor[1]) SDL_FreeCursor(p->mouse.sdl_cursor[1]);
    kdq_destroy(rspice_display_area, p->pixel_queue[0]);
    kdq_destroy(rspice_display_area, p->pixel_queue[1]);
    free(p);

    return 0;
}

rspice_display_pixel *rspice_pixel_get(SpicyConnection *con, int force)
{
    struct spice_connection *c = (struct spice_connection *)con;
    int ara_count;

    c->pixel_painting = 1;
    pthread_mutex_lock(&c->pixel_lock);
    kdq_t(rspice_display_area) *pixel_ara = c->pixel_queue[1];
    c->pixel_queue[1] = c->pixel_queue[0];
    c->pixel_queue[0] = pixel_ara;
    if(c->pixel_data[1])
    {
        RSPICE_FREE_AND_RESET(c->pixel_data[0], free);
        c->pixel_data[0] = c->pixel_data[1];
        c->pixel_data[1] = NULL;
    }
    rspice_display_pixel *pixel_ = c->pixel_data[0];
    pixel_->width = c->d_width;
    pixel_->height = c->d_height;
    pthread_mutex_unlock(&c->pixel_lock);

    ara_count = kdq_size(pixel_ara);

    if (ara_count || force)
    {
        gboolean full_update = 1;

        if (!force && ara_count < _RSPICE_MAX_ARA)
        {
            int pixels_count = 0;
            for (int i = 0; i < ara_count; ++i)
            {
                rspice_display_area *ara = &(kdq_at(pixel_ara, i));
                pixels_count += ara->w * ara->h;
            }
            if (pixels_count < c->d_width * c->d_height * 3 / 5)
                full_update = 0;
        }

        if (full_update)
        {
            pixel_ara->count = 0; // If u updated kdq.h, u may want 2 confirm this still working
            rspice_display_area ara = {0, 0, c->d_width, c->d_height};
            kdq_push(rspice_display_area, pixel_ara, ara);
        }

        for (rspice_display_area *ara; (ara = kdq_shift(rspice_display_area, pixel_ara)) != NULL;)
        {
            char *src = c->d_data; // TODO 可以直接使用d_data, 在primary_create primary_destory 和这里加锁, glTexImage2D GL_RGB GL_BGRA
            char *dest = pixel_->pixel;
            int x, y;
            int stride_dest = c->d_width * 4;
            int stride_src = c->d_width * 4;

            src += ara->y * stride_src + ara->x * 4;
            dest += ara->y * stride_dest + ara->x * 4;
            for (y = 0; y < ara->h; ++y)
            {
                for (x = 0; x < ara->w; ++x) // dest[o_dst + 3] = 0xff; /* memset at malloc */
                {
                    int o_dst = x * 4, o_src = x * 4;
                    dest[o_dst + 0] = src[o_src + 2];
                    dest[o_dst + 1] = src[o_src + 1];
                    dest[o_dst + 2] = src[o_src + 0];
                }
                dest += stride_dest;
                src += stride_src;
            }
        }

        c->pixel_painting = 0;
        return pixel_;
    }

    c->pixel_painting = 0;
    return NULL;
}

int rspice_pixel_updated(SpicyConnection *con)
{
    return kdq_size(((struct spice_connection *)con)->pixel_queue[1]);
}

int rspice_inputs_position(SpicyConnection *con, int x, int y, int mouse_stat)
{
    struct spice_connection *c = (struct spice_connection *)con;

    if (c->inputs)
        spice_inputs_channel_position(c->inputs, x, y, 0 /* only 1 monitor */, mouse_stat);

    return 0;
}

int rspice_inputs_wheel(SpicyConnection *con, int direction, int mouse_stat)
{
    struct spice_connection *c = (struct spice_connection *)con;

    if (c->inputs)
    {
        int wheel_distance = abs(direction);
        int mouse_button = direction > 0 ? SPICE_MOUSE_BUTTON_UP : SPICE_MOUSE_BUTTON_DOWN;

        for (int i = 0; i < wheel_distance; i++)
        {
            spice_inputs_channel_button_press(c->inputs, mouse_button, mouse_stat);
            spice_inputs_channel_button_release(c->inputs, mouse_button, mouse_stat);
        }
    }

    return 0;
}

int rspice_inputs_button_press(SpicyConnection *con, int button, int mouse_stat)
{
    struct spice_connection *c = (struct spice_connection *)con;

    if(c->inputs)
        spice_inputs_channel_button_press(c->inputs, button, mouse_stat);

    return 0;
}

int rspice_inputs_button_release(SpicyConnection *con, int button, int mouse_stat)
{
    struct spice_connection *c = (struct spice_connection *)con;

    if(c->inputs)
        spice_inputs_channel_button_release(c->inputs, button, mouse_stat);

    return 0;
}

rspice_mouse_info *rspice_get_mouse_info(SpicyConnection *con)
{
    struct spice_connection *c = (struct spice_connection *)con;

    c->mouse.cursor_update_state[0] = c->mouse.cursor_update_state[1];
    if (c->mouse.cursor_update_state[1])
    {
        pthread_mutex_lock(&c->mouse_lock);
        c->mouse.cursor_update_state[1] = 0;
        if(c->mouse.sdl_cursor[0])
            SDL_FreeCursor(c->mouse.sdl_cursor[0]);
        c->mouse.sdl_cursor[0] = c->mouse.sdl_cursor[1];
        pthread_mutex_unlock(&c->mouse_lock);
    }

    return &c->mouse;
}

int rspice_inputs_key(SpicyConnection *con, int key, enum sik_type type)
{
    struct spice_connection *c = (struct spice_connection *)con;

    if (!c->inputs || !key)
        return -1;

    switch (type)
    {
    case sik_press:
        spice_inputs_channel_key_press(c->inputs, key);
        break;
    case sik_release:
        spice_inputs_channel_key_release(c->inputs, key);
        break;
    default:
        break;
    }

    return 0;
}

static void refreash_usb_device_list_inner(struct spice_connection *c)
{
    kdq_t(rspice_usb_device) * usb_device_list;
    usb_device_list = kdq_init(rspice_usb_device);

    GPtrArray *devices = spice_usb_device_manager_get_devices(c->usb_manager);
    if (devices != NULL)
    {
        int i;
        for (i = 0; i < devices->len; i++)
        {
            rspice_usb_device _d;
            _d.device = g_boxed_copy(spice_usb_device_get_type(), g_ptr_array_index(devices, i));
            gchar *usb_desc = spice_usb_device_get_description(_d.device, NULL);
            strncpy(_d.description, usb_desc, sizeof(_d.description) - 1);
            _d.description[sizeof(_d.description) - 1] = '\0';
            _d.connected = spice_usb_device_manager_is_device_connected(c->usb_manager, _d.device);
            kdq_push(rspice_usb_device, usb_device_list, _d);
        }

        g_ptr_array_unref(devices);
    }
    pthread_mutex_lock(&c->usb_lock);
    RSPICE_FREE_AND_RESET(c->usb_list[1], device_destroy_list);
    c->usb_list[1] = usb_device_list;
    pthread_mutex_unlock(&c->usb_lock);
}

static void device_error_cb(SpiceUsbDeviceManager *manager, SpiceUsbDevice *device, gpointer user_data)
{
    SPICE_DEBUG("usb device error\n");
}

static void device_added_cb(SpiceUsbDeviceManager *manager, SpiceUsbDevice *device, gpointer user_data)
{
    refreash_usb_device_list_inner((struct spice_connection *)user_data);
}

static void device_removed_cb(SpiceUsbDeviceManager *manager, SpiceUsbDevice *device, gpointer user_data)
{
    refreash_usb_device_list_inner((struct spice_connection *)user_data); /* spice_usb_device_manager_disconnect_device */
}

void rspice_usblist_free(SpicyConnection *con)
{
    struct spice_connection *c = (struct spice_connection *)con;

    if (c->usb_manager && c->usb_sigal_connected)
    {
        c->usb_sigal_connected = 0;
        g_signal_handlers_disconnect_by_func(c->usb_manager, device_added_cb, c);
        g_signal_handlers_disconnect_by_func(c->usb_manager, device_removed_cb, c);
        g_signal_handlers_disconnect_by_func(c->usb_manager, device_error_cb, c);
    }

    pthread_mutex_lock(&c->usb_lock);
    RSPICE_FREE_AND_RESET(c->usb_list[0], device_destroy_list);
    RSPICE_FREE_AND_RESET(c->usb_list[1], device_destroy_list);
    pthread_mutex_unlock(&c->usb_lock);
}

static void usb_connect_cb(GObject *gobject, GAsyncResult *res, gpointer user_data)
{
    SpiceUsbDeviceManager *manager = SPICE_USB_DEVICE_MANAGER(gobject);
    GError *err = NULL;

    spice_usb_device_manager_connect_device_finish(manager, res, &err);
    if (err)
    {
        SPICE_DEBUG("%s", err->message);
        g_error_free(err);
    }
}

int rspice_usb_connect_async(SpicyConnection *con, rspice_usb_device *dev)
{
    struct spice_connection *c = (struct spice_connection *)con;
    GError *err = NULL;

    // make a new refrence of usb device pointer
    if (spice_usb_device_manager_is_redirecting(c->usb_manager) || !spice_usb_device_manager_can_redirect_device(c->usb_manager, dev->device, &err))
        return -1;

    spice_usb_device_manager_connect_device_async(c->usb_manager, dev->device, NULL, usb_connect_cb, NULL);

    return 0;
}

static void usb_disconnect_cb(GObject *gobject, GAsyncResult *res, gpointer user_data)
{
    SpiceUsbDeviceManager *manager = SPICE_USB_DEVICE_MANAGER(gobject);
    GError *err = NULL;

    spice_usb_device_manager_disconnect_device_finish(manager, res, &err);
    if (err)
    {
        SPICE_DEBUG("%s", err->message);
        g_error_free(err);
    }
}

int rspice_usb_disconnect_async(SpicyConnection *con, rspice_usb_device *dev)
{
    struct spice_connection *c = (struct spice_connection *)con;

    if(spice_usb_device_manager_is_redirecting(c->usb_manager) ) /* for some reason, previous connect call may block by low level lib/driver, see https://github.com/torvalds/linux/commit/1530f6f5f5806b2abbf2a9276c0db313ae9a0e09 */
        return -1;
    spice_usb_device_manager_disconnect_device_async(c->usb_manager, dev->device, NULL, usb_disconnect_cb, NULL);

    return 0;
}

kdq_t(rspice_usb_device) * rspice_usblist_get(SpicyConnection *con)
{
    struct spice_connection *c = (struct spice_connection *)con;
    GError *err = NULL;

    if (!c->usb_manager)
    {
        c->usb_manager = spice_usb_device_manager_get(c->session, &err);
        if (err)
        {
            SPICE_DEBUG("%s\n", err->message);
            g_clear_error(&err);
            return NULL;
        }
    }

    if (!c->usb_sigal_connected)
    {
        c->usb_sigal_connected = 1;
        g_signal_connect(c->usb_manager, "device-added", G_CALLBACK(device_added_cb), c);
        g_signal_connect(c->usb_manager, "device-removed", G_CALLBACK(device_removed_cb), c);
        g_signal_connect(c->usb_manager, "device-error", G_CALLBACK(device_error_cb), c);
    }

    if(c->usb_list[0] == NULL)
        refreash_usb_device_list_inner(c);

    if(c->usb_list[1])
    {
        pthread_mutex_lock(&c->usb_lock);
        RSPICE_FREE_AND_RESET(c->usb_list[0], device_destroy_list);
        c->usb_list[0] = c->usb_list[1];
        c->usb_list[1] = NULL;
        pthread_mutex_unlock(&c->usb_lock);
    }

    return c->usb_list[0];
}

const gchar *rspice_get_version_string(void)
{
    return spice_util_get_version_string();
}
