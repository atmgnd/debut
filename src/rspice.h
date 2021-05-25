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
#ifndef _RSPICE_H_B78Y
#define _RSPICE_H_B78Y
#include <stdio.h>
#include <SDL.h>
#include <glib.h>

#include "spice-client.h"

#include "kdq.h"

#ifdef __cplusplus
extern "C"{
#endif
typedef struct _rspice_display_pixel {
    int width;
    int height;
    char pixel[];
}rspice_display_pixel;
typedef struct _SpicyConnection SpicyConnection;

SpicyConnection *rspice_new_connection();
int rspice_main(const char *host, const char *port, SpicyConnection *con);
void rspice_terminate(SpicyConnection *con);
int rspice_free(SpicyConnection *con);
rspice_display_pixel *rspice_pixel_get(SpicyConnection *con, int force);
int rspice_pixel_updated(SpicyConnection *con);
inline void rspice_pixel_free(rspice_display_pixel *p) { };
int rspice_inputs_position(SpicyConnection * con, int x, int y, int mouse_stat);
int rspice_inputs_wheel(SpicyConnection *con, int direction, int mouse_stat);
int rspice_inputs_button_press(SpicyConnection *con, int button, int mouse_stat);
int rspice_inputs_button_release(SpicyConnection *con, int button, int mouse_stat);
const gchar *rspice_get_version_string(void);

enum sms_stae {
    sms_unknow = 0,
    sms_server,
    sms_client
};

typedef struct _rspice_mouse_info{
    enum sms_stae mouse_state;
    int hide_mouse; /* boolean */
    int cursor_update_state[2];
    SDL_Cursor *sdl_cursor[2];
}rspice_mouse_info;

rspice_mouse_info *rspice_get_mouse_info(SpicyConnection * con);

enum sik_type {
    sik_press,
    sik_release
};

int rspice_inputs_key(SpicyConnection * con, int key, enum sik_type type);

typedef struct _rspice_usb_device {
    SpiceUsbDevice *device;
    int connected;
    char description[256];
}rspice_usb_device;

#ifndef _KLIB_IMPL_
KDQ_DECLARE(rspice_usb_device)
#else
KDQ_INIT(rspice_usb_device)
#endif

kdq_t(rspice_usb_device)* rspice_usblist_get(SpicyConnection * con);
void rspice_usblist_free(SpicyConnection * con);
int rspice_usb_connect_async(SpicyConnection * con, rspice_usb_device *dev);
int rspice_usb_disconnect_async(SpicyConnection * con, rspice_usb_device *dev);

void rspice_cache_args(const char *args);
inline void rspice_cache_free(){};

#ifdef __cplusplus
}
#endif

#endif