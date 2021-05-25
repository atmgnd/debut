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

#include <string.h>
#include <stdlib.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <atomic>
#include <algorithm>
#include <vector>
#include <stdint.h>

#include <SDL.h>

#ifndef OSIMP_OPENGL2
#include <GL/gl3w.h> // default imp
#endif

#include "imgui.h"
#include "imgui_impl_sdl.h"
#ifdef OSIMP_OPENGL2
#include "imgui_impl_opengl2.h"
#include <SDL.h>
#include <SDL_opengl.h>
#else
#include <imgui_impl_opengl3.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"

#include "sclient.h"
#include "rspice.h"
#include "rspice_keymap.h"
#include "res.h"

#define SCLIENT_FREE(x) do{if(x){free(x);x = nullptr;}}while(0)
#define SCLIENT_DELETE_GL_TEXTURE(x) do{if(x!=GL_INVALID_VALUE){glDeleteTextures(1, &x);x=GL_INVALID_VALUE;}}while(0)

#define SCLIENT_STYLE_ROUNDING(x1, x2) do{ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, x1);ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, x2);}while(0)
#define SCLIENT_STYLE_BOARDER(x1, x2) do{ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, x1);ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, x2);}while(0)
#define SCLIENT_STYLE_PADDING(x1, y1, x2, y2) do{ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(x1, y1));ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(x2, y2));}while(0)
#define SCLIENT_STYLE_SPACING(x1, x2) ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(x1, x2))

#define TOOLBAR_DENOMINATOR (4)
#define TOOLBAR_COUNT_MAX (120)
#define TOOLBAR_SPACING (2.0f)
#define TOOLBAR_SIZE (48)

static const char *copyright_sign = "Coryright (C) 2019 Qi Zhou. All rights reserved.";

#ifdef WIN32
static HHOOK g_kbdhook = NULL;
static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode < 0 || nCode != HC_ACTION) // do not process message
        return CallNextHookEx(g_kbdhook, nCode, wParam, lParam);

    KBDLLHOOKSTRUCT *p = (KBDLLHOOKSTRUCT *)lParam;

    if(p->vkCode < keymap_win32usb_len && keymap_win32usb[p->vkCode] != SDL_SCANCODE_UNKNOWN)
    {
        SDL_Event sdlevent;
        sdlevent.type = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) ? SDL_KEYUP : SDL_KEYDOWN;
        sdlevent.key.keysym.scancode = (SDL_Scancode)keymap_win32usb[p->vkCode]; /* for now we are not care about SDL_Event.key.keysym.sym or SDL_Event.key.keysym.mod*/
        SDL_PushEvent(&sdlevent);

        return 1;
    }

    return CallNextHookEx(g_kbdhook, nCode, wParam, lParam);
}
#endif

class SClientUI
{
public:
    SClientUI();
    ~SClientUI();
    void SetWindowInfo(SDL_Window *window, int width, int height);
    void PaintLoading();
    void PaintSetup();
    void PaintConnect();
    void PaintDesktop();
    void Paint();
    
    bool GrabKeyboard();
    bool UnGrabKeyboard();

    void SetMisc(bool kiosk, std::string host, std::string port);

private:
    enum UIState
    {
        Invalid,
        Init,
        LoadingBackground,
        Setup,
        Connecting,
        Connected,
        Disconnecting,
        Disconnected,
    } state = UIState::Init;

    enum ToolbarIndex
    {
        Information,
        Usb,
        Fullscreen,
        Terminate,
    };

    struct ToolbarState
    {
        bool alt = false;
        bool open = false;
        bool hide = 0;
        bool reserved = 0;
    };

    GLuint background_texture = GL_INVALID_VALUE;
    unsigned char *background_image = nullptr;
    std::thread background_job;

    SDL_Window *window = nullptr;
    ImVec2 paint_size;
    ImVec2 last_window_size = {1024, 768};
    bool full_screen = false;

    std::thread spice_thread; /* may remove tracking spice thread in future */
    char server_ip[32] = "127.0.0.1";
    char server_port[16] = "5900";
    bool do_connect = false;

    const char *spice_version;
    ImVec2 rspice_offset;
    ImVec2 rspice_size;
    SpicyConnection *rspice_connection = nullptr;
    bool rspice_disconnected = false;
    ToolbarState toolbar_vals[TOOLBAR_DENOMINATOR];
    GLuint toolbar_texture = GL_INVALID_VALUE;
    int show_toolbar = 0;

    GLuint desktop_texture = GL_INVALID_VALUE;
    bool input_grabed = false;
#define SDL_KEY_PRESSTAT_BIT (0x01)
#define SDL_KEY_MODIFIER_BIT (0x02)
    uint8_t sdl_key_state[SDL_NUM_SCANCODES] = {};
};

SClientUI::SClientUI()
{
    spice_version = rspice_get_version_string();
}

SClientUI::~SClientUI()
{
    if (background_job.joinable()) background_job.detach(); /* let the system to kill thread */
    if (spice_thread.joinable()) spice_thread.detach();
    SCLIENT_FREE(background_image);
    SCLIENT_DELETE_GL_TEXTURE(background_texture);
}

inline ImVec2 calc_rspice_offset(ImVec2 l, ImVec2 r)
{
    return (r.x < l.x && r.y < l.y) ? ImVec2((l.x - r.x) / 2, (l.y - r.y) / 2) : ImVec2(0, 0);
}

void SClientUI::SetWindowInfo(SDL_Window *window, int width, int height)
{
    this->window = window;
    paint_size = ImVec2(width, height);
    rspice_offset = calc_rspice_offset(paint_size, rspice_size);
    if((this->full_screen = SDL_GetWindowFlags(window) & (SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_FULLSCREEN)) == false)
        last_window_size = paint_size;
}

void SClientUI::PaintLoading()
{
    SCLIENT_STYLE_ROUNDING(0.0f, 0.0f);
    SCLIENT_STYLE_BOARDER(0.0f, 0.0f);
    SCLIENT_STYLE_PADDING(0.0f, 0.0f, 0.0f, 0.0f);
#define LOAD_RECT_COUNT (10)
    float line_h = ImGui::GetTextLineHeight(), gap_w = 2.0f;
    int idx = (ImGui::GetFrameCount() / 10) % LOAD_RECT_COUNT;
    ImGui::SetNextWindowPos(ImVec2(paint_size.x / 2 , paint_size.y / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(line_h * LOAD_RECT_COUNT + gap_w * 9, line_h * 4), ImGuiCond_Always);
    ImGui::Begin("##loading", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.94f, 0.94f, 1.00f));
    ImGui::Text("Loading...");
    ImGui::PopStyleColor(1);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    for(int i = 0; i < LOAD_RECT_COUNT;i ++)
    {
        float off_w = i * (line_h + gap_w);
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x + off_w, pos.y), ImVec2(pos.x + off_w + line_h, pos.y + line_h), (i == idx) ? IM_COL32(210, 105, 30, 255) : IM_COL32(41, 36, 33, 255));
    }
    ImGui::End();
    ImGui::PopStyleVar(6);
}

void SClientUI::PaintSetup()
{
    SCLIENT_STYLE_ROUNDING(0.0f, 0.0f);
    SCLIENT_STYLE_BOARDER(0.0f, 0.0f);
    SCLIENT_STYLE_PADDING(0.0f, 0.0f, 0.0f, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(paint_size, ImGuiCond_Always);
    ImGui::Begin("##background_setup", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::Image((void *)(intptr_t)background_texture, paint_size); /* assert(ImGui::GetContentRegionAvail() == paint_size) */
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::Begin("##coryright", NULL, ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground);
    ImGui::Text("%s", copyright_sign);
    ImGui::End();
    ImGui::PopStyleVar(6);

    ImGui::SetNextWindowPos(ImVec2(paint_size.x / 2 , paint_size.y * 2 / 5), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGui::Begin("##connect_window", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove );//| ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PushItemWidth(108);
    ImGui::Text("Hostname");ImGui::SameLine();ImGui::Indent(72);
    ImGui::InputText("##host name", server_ip, IM_ARRAYSIZE(server_ip));ImGui::Unindent(72);
    ImGui::Text("Port");ImGui::SameLine();ImGui::Indent(72);
    ImGui::InputText("##server port", server_port, IM_ARRAYSIZE(server_port));ImGui::Unindent(72);
    ImGui::PopItemWidth();

    ImGui::Indent(ImGui::GetWindowWidth() - 74);
    if (ImGui::Button("connect"))
    {
        SCLIENT_DELETE_GL_TEXTURE(background_texture);
        do_connect = true;
    }
    ImGui::Unindent(ImGui::GetWindowWidth() - 74);
    ImGui::End();
    ImGui::PopStyleColor(2);
}

void SClientUI::PaintConnect()
{
    return PaintLoading();
}

void SClientUI::PaintDesktop()
{
    rspice_display_pixel *new_pixel = rspice_pixel_get(rspice_connection, 0);
    if (new_pixel) // TODO https://www.khronos.org/opengl/wiki/Buffer_Object_Streaming
    {
        glActiveTexture(GL_TEXTURE0);
        if (desktop_texture == GL_INVALID_VALUE || rspice_size.x != new_pixel->width || rspice_size.y != new_pixel->height)
        {
            SCLIENT_DELETE_GL_TEXTURE(desktop_texture);
            glGenTextures(1, &desktop_texture);
            glBindTexture(GL_TEXTURE_2D, desktop_texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, new_pixel->width, new_pixel->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, new_pixel->pixel); /* src pixel need to be aligned to 4, see glPixelStorei(GL_UNPACK_ALIGNMENT, 1) */
            rspice_size = ImVec2(new_pixel->width, new_pixel->height);
            rspice_offset = calc_rspice_offset(paint_size, rspice_size);
        }
        else
        { // TODO partial update
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, new_pixel->width, new_pixel->height, GL_RGBA, GL_UNSIGNED_BYTE, new_pixel->pixel);
        }

        rspice_pixel_free(new_pixel);
    }

    SCLIENT_STYLE_ROUNDING(0.0f, 0.0f);
    SCLIENT_STYLE_BOARDER(0.0f, 0.0f);
    SCLIENT_STYLE_PADDING(0.0f, 0.0f, 0.0f, 0.0f);

    ImGui::SetNextWindowPos(rspice_offset, ImGuiCond_Always);
    ImGui::SetNextWindowSize(rspice_size, ImGuiCond_Always);
    ImGui::Begin("##rspice_pixel", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground);
    ImGui::Image((void *)(intptr_t)desktop_texture, rspice_size);
    ImGui::End();
    ImGui::PopStyleVar(4);

    ImGuiIO &io = ImGui::GetIO();
    for (unsigned int i = 0; i < keymap_usb2xtkbd_len; i++)
    {
        if (ImGui::IsKeyPressed(i))
        {
            if (!(sdl_key_state[i] & SDL_KEY_MODIFIER_BIT) || !(sdl_key_state[i] & SDL_KEY_PRESSTAT_BIT))
            {
                rspice_inputs_key(rspice_connection, keymap_usb2xtkbd[i], sik_press);
                sdl_key_state[i] |= SDL_KEY_PRESSTAT_BIT;
            }
        }
        if (ImGui::IsKeyReleased(i))
        {
            rspice_inputs_key(rspice_connection, keymap_usb2xtkbd[i], sik_release);
            sdl_key_state[i] &= (~SDL_KEY_PRESSTAT_BIT);
        }
    }

    int mouse_x = io.MousePos.x, mouse_y = io.MousePos.y;
#ifndef _DONOT_GRAB_INPUT
    if (!full_screen)
    {
        int input_grab_case = (input_grabed ? 1 : 0) | ((mouse_x > 0 && mouse_y > 0 && mouse_x < paint_size.x - 1 && mouse_y < paint_size.y - 1) ? 2 : 0);
        switch (input_grab_case)
        {
        case 1: /* keyboard is graned, but mouse is not over window */
            UnGrabKeyboard(); /* Ungrab if remote mouse is not drawing ? */
            break;
        case 2: /* keyboard is not grabed, and mouse is over the window */
            GrabKeyboard();
            break;
        }
    }
#endif

    bool capture_mouse = mouse_x >= rspice_offset.x && mouse_x <= rspice_offset.x + rspice_size.x &&  mouse_y >= rspice_offset.y && mouse_y <= rspice_offset.y + rspice_size.y;
    if(show_toolbar > 0)
    {
        glActiveTexture(GL_TEXTURE1);
        if (toolbar_texture == GL_INVALID_VALUE)
        {
            int tw, th, tcomp;
            unsigned char *timage = stbi_load_from_memory(res_toolbar_png, res_toolbar_png_len, &tw, &th, &tcomp, STBI_rgb_alpha);

            glGenTextures(1, &toolbar_texture);
            glBindTexture(GL_TEXTURE_2D, toolbar_texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, timage);
            stbi_image_free(timage);
        }

        SCLIENT_STYLE_PADDING(TOOLBAR_SPACING, TOOLBAR_SPACING, 0.0f, 0.0f);
        SCLIENT_STYLE_SPACING(TOOLBAR_SPACING, TOOLBAR_SPACING);
        ImGui::SetNextWindowPos(ImVec2(paint_size.x / 2, 0.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::Begin("##toolbar", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
        for(int i = 0;i < TOOLBAR_DENOMINATOR; i++)
        {
            if(toolbar_vals[i].hide)
                continue;
            float shift_y = toolbar_vals[i].alt ? 0.5f : 0.0f;
            ImGui::PushID(i);
            if (ImGui::ImageButton((void *)(intptr_t)(toolbar_texture), ImVec2(TOOLBAR_SIZE, TOOLBAR_SIZE), ImVec2(i / (float)TOOLBAR_DENOMINATOR, 0.0f + shift_y), ImVec2((i + 1) / (float)TOOLBAR_DENOMINATOR, 0.5f + shift_y), 1.0f, ImVec4(1.0f, 1.0f, 1.0f, 0.7f)))
                toolbar_vals[i].open = true;
            ImGui::PopID();
            if (i < TOOLBAR_DENOMINATOR - 1) ImGui::SameLine();
        }
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
        {
            show_toolbar = TOOLBAR_COUNT_MAX;
            capture_mouse = false;
        }
        else
        {
            show_toolbar--;
        }
        ImGui::End();
        ImGui::PopStyleVar(3);
    }
    else if(mouse_x >= (paint_size.x / 2 - 75)  && mouse_y >= 0 && mouse_x < (paint_size.x / 2 + 75) && mouse_y < 3)
    {
        show_toolbar = TOOLBAR_COUNT_MAX; // show toolbar at future frames
    }

    if (toolbar_vals[ToolbarIndex::Fullscreen].open)
    {
        toolbar_vals[ToolbarIndex::Fullscreen].alt = full_screen;
        SDL_SetWindowFullscreen(window, !full_screen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0); // SDL_WINDOW_FULLSCREEN_DESKTOP
        if ((full_screen = !full_screen) == false)
            SDL_SetWindowSize(window, last_window_size.x, last_window_size.y);

        toolbar_vals[ToolbarIndex::Fullscreen].open = false;
    }

    if (toolbar_vals[ToolbarIndex::Usb].open)
    {
        ImGui::SetNextWindowPos(ImVec2(paint_size.x / 2, paint_size.y / 2), ImGuiCond_Once, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowBgAlpha(0.88f);
        ImGui::Begin("Select usb devices to redirect", &toolbar_vals[ToolbarIndex::Usb].open, /* ImGuiWindowFlags_NoTitleBar | */ ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse);
        kdq_t(rspice_usb_device) *usb_list = rspice_usblist_get(rspice_connection);
        if (kdq_size(usb_list) == 0)
            ImGui::Text("No usb devices detected!");
        else
            for (size_t i = 0; usb_list && i < kdq_size(usb_list); i++)
            {
                rspice_usb_device *_d = &(kdq_at(usb_list, i));
                bool checked = !!_d->connected;
                if (ImGui::Checkbox(_d->description, &checked))
                {
                    // more check, redirectable ? grayout checkbox(gui not supported for now), race condiction...
                    if (!(checked ? rspice_usb_connect_async(rspice_connection, _d) : rspice_usb_disconnect_async(rspice_connection, _d)))
                        _d->connected = checked;
                }
            }

        if (!toolbar_vals[ToolbarIndex::Usb].open) /* If close is clicked, the gui will set show_usb_dialog properly */
            rspice_usblist_free(rspice_connection);

        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
            capture_mouse = false;
        ImGui::End();
    }

    if (toolbar_vals[ToolbarIndex::Information].open)
    {
        ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.94f, 0.94f, 0.94f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.94f, 0.94f, 0.94f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.94f, 0.94f, 0.94f, 1.00f));
        ImGui::SetNextWindowPos(ImVec2(paint_size.x - 10, 10), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::Begin("Info", &toolbar_vals[ToolbarIndex::Information].open, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("%s ", copyright_sign);
        ImGui::Separator();
        ImGui::Text("framerate: %.1f", io.Framerate);
        ImGui::Separator();
        ImGui::Text("spice version: %s", spice_version);
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
            capture_mouse = false;
        ImGui::End();
        ImGui::PopStyleColor(3);
    }
    ImGui::PopStyleVar(2);

    if (capture_mouse) // client mouse mode only
    {
        bool cursor_captured = io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

        rspice_mouse_info *rspice_mouse = rspice_get_mouse_info(rspice_connection);
        int mouse_ste = 0;
        static const int spice_mouse_stat_map[] = {
            [0] = SPICE_MOUSE_BUTTON_MASK_LEFT,
            [1] = SPICE_MOUSE_BUTTON_MASK_RIGHT,
            [2] = SPICE_MOUSE_BUTTON_MASK_MIDDLE,
        };

        for (int i = 0; i < 3; i++)
        {
            if (io.MouseDown[i])
                mouse_ste |= spice_mouse_stat_map[i];
        }

        if (rspice_mouse->mouse_state == sms_client)
        {
            static const int spice_mouse_map[] = {
                [0] = SPICE_MOUSE_BUTTON_LEFT,
                [1] = SPICE_MOUSE_BUTTON_RIGHT,
                [2] = SPICE_MOUSE_BUTTON_MIDDLE,
            };

            for (int i = 0; i < 3; i++)
            {
                if (io.MouseClicked[i])
                    rspice_inputs_button_press(rspice_connection, spice_mouse_map[i], mouse_ste);

                if (io.MouseReleased[i])
                    rspice_inputs_button_release(rspice_connection, spice_mouse_map[i], mouse_ste);
            }

            if (io.MouseWheel)
                rspice_inputs_wheel(rspice_connection, io.MouseWheel, mouse_ste);

            if (io.MouseDelta.x || io.MouseDelta.y)
                rspice_inputs_position(rspice_connection, mouse_x - rspice_offset.x, mouse_y - rspice_offset.y, mouse_ste);

            if (!rspice_mouse->hide_mouse)
            {
                if ((rspice_mouse->cursor_update_state[0] || cursor_captured == false) && rspice_mouse->sdl_cursor[0] )
                    SDL_SetCursor(rspice_mouse->sdl_cursor[0]);
                SDL_ShowCursor(SDL_TRUE);
            }
            else
            {
                SDL_ShowCursor(SDL_FALSE);
            }
        }
        else if (rspice_mouse->mouse_state == sms_server && 0 /* not implementated yet */)
        {
        }
    }
    else
    {
        io.ConfigFlags &= (~ImGuiConfigFlags_NoMouseCursorChange);
    }
}

void calc_span_point(ImVec2 src_size, ImVec2 dst_size, ImVec2 *p1, ImVec2 *p2)
{
    float wr = (float)src_size.x / dst_size.x, hr = (float)src_size.y / dst_size.y;
    p2->x = (wr > hr) ? ((float)dst_size.x / dst_size.y * src_size.y / src_size.x) : 1;
    p2->y = (wr < hr) ? ((float)dst_size.y / dst_size.x * src_size.x / src_size.y) : 1;

    p1->x = (1 - p2->x) / 2;
    p2->x += p1->x;
    p1->y = (1 - p2->y) / 2;
    p2->y += p1->y;
}

void SClientUI::Paint()
{
    switch (state)
    {
    case UIState::Init:
        SDL_SetWindowResizable(window, SDL_FALSE);
        reset_key_statemap_modifies(sdl_key_state, sizeof(sdl_key_state), SDL_KEY_MODIFIER_BIT);
        show_toolbar = 0;
        for(int i = 0; i < TOOLBAR_DENOMINATOR; i++)
            toolbar_vals[i].alt = toolbar_vals[i].open = false;
        toolbar_vals[ToolbarIndex::Fullscreen].alt = !full_screen;
        background_job = std::thread([this, w2 = paint_size.x, h2 = paint_size.y]() {
            int orig_width, orig_height, comp;
            ImVec2 p1, p2;

            SCLIENT_FREE(background_image);
            unsigned char *image_orig = stbi_load_from_memory(res_setup_jpg, res_setup_jpg_len, &orig_width, &orig_height, &comp, STBI_rgb_alpha);
            unsigned char *image_span = (unsigned char *)malloc(w2 * h2 * STBI_rgb_alpha * sizeof(unsigned char));
            calc_span_point(ImVec2(orig_width, orig_height), ImVec2(w2, h2), &p1, &p2);
            stbir_resize_region(image_orig, orig_width, orig_height, 0, image_span, w2, h2, 0, STBIR_TYPE_UINT8, STBI_rgb_alpha,
                                STBIR_ALPHA_CHANNEL_NONE, 0 /* flags */, STBIR_EDGE_CLAMP, STBIR_EDGE_CLAMP, STBIR_FILTER_DEFAULT,
                                STBIR_FILTER_DEFAULT, STBIR_COLORSPACE_LINEAR, nullptr, p1.x, p1.y, p2.x, p2.y);
            stbi_image_free(image_orig);
            background_image = image_span;
        });
        state = UIState::LoadingBackground;
        break;
    case UIState::LoadingBackground:
        PaintLoading();
        if (background_image != nullptr)
        {
            background_job.join();
            SCLIENT_DELETE_GL_TEXTURE(background_texture);
            glGenTextures(1, &background_texture);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, background_texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, paint_size.x, paint_size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, background_image);
            state = UIState::Setup;
            SCLIENT_FREE(background_image);
        }
        break;
    case UIState::Setup:
        PaintSetup();
        if (do_connect)
        {
            rspice_disconnected = false;
            spice_thread = std::thread([this]() {
                rspice_connection = rspice_new_connection();
                rspice_main(server_ip, server_port, rspice_connection);
                rspice_disconnected = true;
            });
            state = UIState::Connecting;
            do_connect = false;
        }
        break;
    case UIState::Connecting:
        PaintConnect();
        if (rspice_disconnected)
        {
            state = UIState::Disconnected;
        }
        else if (rspice_connection != nullptr && rspice_pixel_updated(rspice_connection))
        {
            SDL_SetWindowResizable(window, SDL_TRUE);
#ifndef _DONOT_GRAB_INPUT
            if (full_screen)
                GrabKeyboard();
#endif
            state = UIState::Connected;
        }
        break;
    case UIState::Connected:
        PaintDesktop();
        if (toolbar_vals[ToolbarIndex::Terminate].open || rspice_disconnected)
        {
#ifndef _DONOT_GRAB_INPUT
            UnGrabKeyboard();
#endif
            state = rspice_disconnected ? UIState::Disconnected : UIState::Disconnecting;
        }
        break;
    case UIState::Disconnecting:
        rspice_terminate(rspice_connection);
        state = UIState::Disconnected;
        break;
    case UIState::Disconnected:
        ImGui::GetIO().ConfigFlags &= (~ImGuiConfigFlags_NoMouseCursorChange);
        state = UIState::Init;
        spice_thread.join();

        SCLIENT_DELETE_GL_TEXTURE(desktop_texture);
        SCLIENT_DELETE_GL_TEXTURE(toolbar_texture);
        rspice_free(rspice_connection);
        rspice_connection = nullptr;
#ifdef EXIT_AFTER_SPICE_LOST
        exit(0);
#endif
        break;
    default:
        break;
    }
}

bool SClientUI::GrabKeyboard()
{
    input_grabed = true;
#if defined(__linux__)
    SDL_SetWindowGrab(window, SDL_TRUE);
#elif defined(WIN32)
    if(g_kbdhook == NULL)
        g_kbdhook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
#endif
    return true;
}

bool SClientUI::UnGrabKeyboard()
{
    input_grabed = false;
#if defined(__linux__)
    SDL_SetWindowGrab(window, SDL_FALSE);
#elif defined(WIN32)
    if (g_kbdhook)
    {
        g_kbdhook = NULL;
        UnhookWindowsHookEx(g_kbdhook);
    }
#endif
    return true;
}

void SClientUI::SetMisc(bool kiosk, std::string host, std::string port)
{
    toolbar_vals[ToolbarIndex::Fullscreen].hide = kiosk;
    if (host.size() && host.size() < 32 && port.size() && port.size() < 16)
    {
        do_connect = true;
        strcpy(this->server_ip, host.c_str());
        strcpy(this->server_port, port.c_str());
    }
}

static SClientUI g_inst;

void sclient_set_paintinfo(SDL_Window *window, int width, int height)
{
    g_inst.SetWindowInfo(window, width, height);
}

void sclient_paint_one_frame()
{
    g_inst.Paint();
}

void sclient_cleanup()
{
#ifndef _DONOT_GRAB_INPUT
    g_inst.UnGrabKeyboard();
#endif
}

void sclient_set_misc(bool kiosk, std::string host, std::string port)
{
    g_inst.SetMisc(kiosk, host, port);
}
