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
#ifndef _RES_H_XXDC
#define _RES_H_XXDC
#ifdef __cplusplus
extern "C"{
#endif

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#ifndef RES_ROOT_PATH
#define RES_ROOT_PATH ./res/
#endif

#define RES_PATH(x) TOSTRING(RES_ROOT_PATH) #x

/* use RES(setup.jpg) to get a const c string about setup.jpg */
extern unsigned char res_Roboto_Medium_ttf[];
extern unsigned int res_Roboto_Medium_ttf_len;

extern unsigned char res_setup_jpg[];
extern unsigned int res_setup_jpg_len;

extern unsigned char res_toolbar_png[];
extern unsigned int res_toolbar_png_len;

#ifdef __cplusplus
}
#endif
#endif