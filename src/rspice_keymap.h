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

#ifndef _RSPICE_KEYMAP_H_DDGB
#define _RSPICE_KEYMAP_H_DDGB
#include <SDL.h>

#ifdef __cplusplus
extern "C"{
#endif

inline void reset_key_statemap_modifies(uint8_t maps[], size_t sz, uint8_t mask)
{
   memset(maps, 0, sz); /* assert(sz >= SDL_NUM_SCANCODES) */
   maps[SDL_SCANCODE_LCTRL] |= mask;
   maps[SDL_SCANCODE_LSHIFT] |= mask;
   maps[SDL_SCANCODE_LALT] |= mask;
   maps[SDL_SCANCODE_LGUI] |= mask;
   maps[SDL_SCANCODE_RCTRL] |= mask;
   maps[SDL_SCANCODE_RSHIFT] |= mask;
   maps[SDL_SCANCODE_RALT] |= mask;
   maps[SDL_SCANCODE_RGUI] |= mask;
}

extern const unsigned short keymap_usb2xtkbd[];
extern const unsigned int keymap_usb2xtkbd_len;
#ifdef WIN32
extern const unsigned short keymap_win32usb[];
extern const unsigned int keymap_win32usb_len;
#endif

#ifdef __cplusplus
}
#endif
#endif