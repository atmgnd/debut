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

#ifndef _SCLIENT_H_HFTV
#define _SCLIENT_H_HFTV

#include <string>

void sclient_set_paintinfo(SDL_Window *window, int width, int height);
void sclient_paint_one_frame();
void sclient_cleanup();
void sclient_set_misc(bool kiosk, std::string host, std::string port);

#endif
