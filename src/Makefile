#
# Cross Platform Makefile
# Compatible with MSYS2/MINGW, Ubuntu 14.04.1 and Mac OS X
#
# You will need SDL2 (http://www.libsdl.org):
# Linux:
#   apt-get install libsdl2-dev
# Mac OS X:
#   brew install sdl2
# MSYS2:
#   pacman -S mingw-w64-i686-SDL
#

#CXX = g++
#CXX = clang++

EXE = sclient
SOURCES = main_sdl2_opengl3.cpp sclient.cpp rspice.c rspice_keymap_usb2xtkbd.c res.c
SOURCES += deps/imgui/examples/imgui_impl_sdl.cpp deps/imgui/examples/imgui_impl_opengl3.cpp
SOURCES += deps/imgui/imgui.cpp deps/imgui/imgui_demo.cpp deps/imgui/imgui_draw.cpp deps/imgui/imgui_widgets.cpp
OBJS = $(addsuffix .o, $(basename $(notdir $(SOURCES))))
UNAME_S = $(shell uname -s)

CXXFLAGS += -Ideps/imgui/examples -Ideps/imgui -Ideps
CXXFLAGS += -g -Wall -Wformat
LIBS =

##---------------------------------------------------------------------
## OPENGL LOADER
##---------------------------------------------------------------------

## Using OpenGL loader: gl3w [default]
SOURCES += deps/imgui/examples/libs/gl3w/GL/gl3w.c
CXXFLAGS += -Ideps/imgui/examples/libs/gl3w -DIMGUI_IMPL_OPENGL_LOADER_GL3W

## Using OpenGL loader: glew
## (This assumes a system-wide installation)
# CXXFLAGS += -lGLEW -DIMGUI_IMPL_OPENGL_LOADER_GLEW

## Using OpenGL loader: glad
# SOURCES += ../libs/glad/src/glad.c
# CXXFLAGS += -I../libs/glad/include -DIMGUI_IMPL_OPENGL_LOADER_GLAD

##---------------------------------------------------------------------
## BUILD FLAGS PER PLATFORM
##---------------------------------------------------------------------

SDL2_CONFIG=sdl2-config

ifeq ($(UNAME_S), Linux) #LINUX
	ECHO_MESSAGE = "Linux"
	LIBS += -lGL -ldl -pthread `$(SDL2_CONFIG) --libs` `pkg-config --libs spice-client-glib-2.0 spice-protocol glib-2.0 gio-2.0 libusbredirparser-0.5 libusbredirhost libusb-1.0 pixman-1 libjpeg libssl libcrypto opus gstreamer-1.0 gstreamer-audio-1.0 gstreamer-app-1.0 gstreamer-video-1.0 libpulse libpulse-mainloop-glib zlib`

	CXXFLAGS += `$(SDL2_CONFIG) --cflags` `pkg-config --cflags spice-client-glib-2.0 spice-protocol glib-2.0`
	CFLAGS = $(CXXFLAGS)
endif

ifeq ($(UNAME_S), Darwin) #APPLE
	ECHO_MESSAGE = "Mac OS X"
	LIBS += -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo `sdl2-config --libs`
	LIBS += -L/usr/local/lib -L/opt/local/lib

	CXXFLAGS += `sdl2-config --cflags`
	CXXFLAGS += -I/usr/local/include -I/opt/local/include
	CFLAGS = $(CXXFLAGS)
endif

ifeq ($(findstring MINGW,$(UNAME_S)),MINGW)
   ECHO_MESSAGE = "MinGW"
   SOURCES += rspice_keymap_win32usb.c
   LIBS += -lgdi32 -lopengl32 -limm32 -lIPHLPAPI `$(SDL2_CONFIG) --libs` `pkg-config --libs spice-client-glib-2.0 spice-protocol glib-2.0 gio-2.0 libusbredirparser-0.5 libusbredirhost libusb-1.0 pixman-1 libjpeg libssl libcrypto opus gstreamer-1.0 gstreamer-audio-1.0 gstreamer-app-1.0 gstreamer-video-1.0`

   CXXFLAGS += -DEXIT_AFTER_SPICE_LOST `$(SDL2_CONFIG) --cflags` `pkg-config --cflags spice-client-glib-2.0 spice-protocol glib-2.0`
   CFLAGS = $(CXXFLAGS)
   OBJS += sclient.res
endif

##---------------------------------------------------------------------
## BUILD RULES
##---------------------------------------------------------------------

%.o:%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o:deps/imgui/examples/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o:deps/imgui/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o:deps/imgui/examples/libs/gl3w/GL/%.c
	$(CC) $(CFLAGS) -c -o $@ $<
    
%.res:%.rc
	windres -i $< --input-format=rc -o $@ -O coff

all: $(EXE)
	@echo Build complete for $(ECHO_MESSAGE)

$(EXE): $(OBJS)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LIBS)

res.c: res.h
	xxd -i res/Roboto-Medium.ttf > res.c
	xxd -i res/setup.jpg >> res.c
	xxd -i res/toolbar.png >> res.c

KEYMAP_GEN = deps/keycodemapdb/tools/keymap-gen
KEYMAP_CSV = deps/keycodemapdb/data/keymaps.csv
rspice_keymap_usb2xtkbd.c:
	$(KEYMAP_GEN) --lang=stdc --varname=keymap_usb2xtkbd code-map $(KEYMAP_CSV) usb xtkbd > $@ || rm $@

rspice_keymap_win32usb.c:
	$(KEYMAP_GEN) --lang=stdc --varname=keymap_win32usb code-map $(KEYMAP_CSV) win32 usb > $@ || rm $@

clean:
	rm -f $(EXE) $(OBJS)
