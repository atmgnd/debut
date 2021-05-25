#!/bin/sh
# Use -rpath/-rpath-link linker option:
# 1. gcc XXX.c -o xxx.out -L$HOME/.usr/lib -lXX -Wl,-rpath=/home/user/.usr/lib
# 2. Use LD_LIBRARY_PATH environment variable - put this line in your ~/.bashrc file:
# linux requirements:
#   gstreamer1.0 gstreamer1.0-tools gstreamer1.0-plugins-bad gstreamer1.0-plugins-good
#   gstreamer1.0-plugins-base gstreamer1.0-libav ...
# debian 10 deps:
#   build-essential pkg-config bison flex python3-pip meson
#   libglib2.0-dev libpixman-1-dev python3-pyparsing python3-six libcairo2-dev
#   libjpeg62-turbo-dev libjson-glib-dev libgstreamer-plugins-base1.0-dev libgstreamer1.0-dev
#   gstreamer1.0-plugins-good libepoxy-dev libgbm-dev libopus-dev liblz4-dev libsasl2-dev linux-libc-dev
# mingw32 requirements:
#   base-devel glib2 glib2-devel unzip
#   mingw32/mingw-w64-i686-SDL2 mingw32/mingw-w64-i686-pixman mingw32/mingw-w64-i686-glib2
#   mingw32/mingw-w64-i686-libjpeg-turbo mingw32/mingw-w64-i686-cairo mingw32/mingw-w64-i686-json-glib
#   mingw-w64-i686-gst-libav mingw-w64-i686-gst-plugins-bad mingw-w64-i686-gst-plugins-base
#   mingw-w64-i686-gst-plugins-good mingw-w64-i686-gst-plugins-ugly mingw-w64-i686-gstreamer
#   mingw-w64-i686-icoutils mingw-w64-i686-python-six mingw-w64-i686-python-pyparsing mingw-w64-i686-toolchain
# mingw64 requirements:
#   base-devel glib2 glib2-devel unzip
#   mingw-w64-x86_64-gst-libav mingw-w64-x86_64-gst-plugins-bad mingw-w64-x86_64-gst-plugins-base
#   mingw-w64-x86_64-gst-plugins-good mingw-w64-x86_64-gst-plugins-ugly mingw-w64-x86_64-gstreamer
#   mingw-w64-x86_64-icoutils mingw-w64-x86_64-python-six mingw-w64-x86_64-python-pyparsing
# example of setup run-time mingw env
#   export PKG_CONFIG_PATH=`readlink -f ../sysroot/lib/pkgconfig`:`readlink -f ../sysroot/share/pkgconfig` LD_LIBRARY_PATH=`readlink -f ../sysroot/lib`
#   export PATH=$PATH:`readlink -f ../sysroot/bin`
# example of setup run-time linux env
#   export PKG_CONFIG_PATH=`readlink -f ../sysroot/lib/pkgconfig`:`readlink -f ../sysroot/share/pkgconfig` LD_LIBRARY_PATH=`readlink -f ../sysroot/lib`
# export PKG_CONFIG_LIBDIR if doing crosscompile
# Usage examples:
#   BD_DLDIR=/where/source_packages BD_SYSROOT=/xxx/prefix /home/xxx/build_spice.sh

BD_BUILDIR=`pwd`
BD_ME=`readlink -f "$0"`
export BD_DIR=`dirname "${BD_ME}"`
export BD_DLDIR=${BD_DLDIR:-${BD_DIR}}
export BD_SYSROOT=${BD_SYSROOT:-${BD_BUILDIR}/sysroot}
CC_MACHINE=`gcc -dumpmachine`
export PKG_CONFIG_PATH=${BD_SYSROOT}/lib/pkgconfig:${BD_SYSROOT}/share/pkgconfig:${BD_SYSROOT}/lib/${CC_MACHINE}/pkgconfig
export LD_LIBRARY_PATH=${BD_SYSROOT}/lib:${BD_SYSROOT}/lib/${CC_MACHINE}
export PATH=${BD_SYSROOT}/bin:${PATH}

echo export PKG_CONFIG_PATH=${PKG_CONFIG_PATH}
echo export LD_LIBRARY_PATH=${LD_LIBRARY_PATH}
echo export PATH=${PATH}

UNAME_MO=`uname -mo`
case "${UNAME_MO}" in
	"x86_64 Msys")
		SPICE_GLIB_USBIDS="./usb.ids"
		OPENSSL_CFG="mingw"
		;;
	"x86_64 GNU/Linux")
		SPICE_GLIB_USBIDS="auto"
		OPENSSL_CFG="linux-x86_64"
		;;		
	"aarch64 GNU/Linux")
		SPICE_GLIB_USBIDS="auto"
		OPENSSL_CFG="linux-aarch64"
		;;
	*)
		echo unsupported machine type
		exit 1
		;;
esac

mkdir -p ${BD_SYSROOT}

# build libusb
if test "${BUILD_LIBUSB}" = "1" -o "${BUILD_ALL}" = "1"
then
	cd "${BD_BUILDIR}"
	tar xjvf "${BD_DLDIR}/libusb-1.0.23.tar.bz2"
	cd libusb-1.0.23
	./configure --prefix=${BD_SYSROOT} --enable-examples-build --enable-tests-build --disable-udev || exit 1
	make -j8 && make install || exit 1
fi

# build usbredir
if test "${BUILD_USBREDIR}" = "1" -o "${BUILD_ALL}" = "1"
then
	cd "${BD_BUILDIR}"
	tar xjvf "${BD_DLDIR}/usbredir-0.8.0.tar.bz2"
	cd usbredir-0.8.0/
	./configure --prefix=${BD_SYSROOT} CFLAGS=-Wno-format-truncation || exit 1
	make -j 4 usbredirparser && make install
	make -j 4 && make install || exit 1
fi

# build openssl, do not build openssl by default
if test "${BUILD_OPENSSL}" = "1"
then
	cd "${BD_BUILDIR}"
	tar xzvf "${BD_DLDIR}/openssl-1.1.0h.tar.gz"
	cd openssl-1.1.0h/
	./Configure no-idea no-mdc2 no-rc5 ${OPENSSL_CFG} --prefix=${BD_SYSROOT} || exit 1
	make -j8 && make install || exit 1
fi

# build spice-protocol
if test "${BUILD_SPICE_PROTOCOL}" = "1" -o "${BUILD_ALL}" = "1"
then
	cd "${BD_BUILDIR}"
	tar xJvf "${BD_DLDIR}/spice-protocol-0.14.3.tar.xz"
	cd spice-protocol-0.14.3/
	meson setup --prefix "${BD_SYSROOT}" --backend ninja build
	ninja -C build
	ninja -C build install || exit 1
fi

# build spice-gtk only glib
if test "${BUILD_SPICE_GTK}" = "1" -o "${BUILD_ALL}" = "1"
then
	cd "${BD_BUILDIR}"
	tar xJvf "${BD_DLDIR}/spice-gtk-0.39.tar.xz"
	cd spice-gtk-0.39/
	meson setup --prefix "${BD_SYSROOT}" --backend ninja -Dusbredir=enabled -Dgtk=enabled -Dcelt051=disabled -Dopus=disabled -Dpulse=disabled -Dspice-common:tests=false -Dspice-common:manual=false -Dwayland-protocols=disabled build
	ninja -C build
	ninja -C build install || exit 1
fi

# build spice server lib
if test "${BUILD_SPICE}" = "1" -o "${BUILD_ALL}" = "1"
then
	cd "${BD_BUILDIR}"
	tar xjvf "${BD_DLDIR}/spice-0.14.3.tar.bz2"
	cd spice-0.14.3
	meson setup --prefix "${BD_SYSROOT}" --backend ninja -Dspice-common:tests=false build
	ninja -C build
	ninja -C build install || exit 1
fi

# build sdl if needed
if test "${BUILD_SDL}" = "1" -o "${BUILD_ALL}" = "1"
then
	cd "${BD_BUILDIR}"
	tar xzvf "${BD_DLDIR}/SDL2-2.0.10.tar.gz"
	cd SDL2-2.0.10/
	./configure --prefix=${BD_SYSROOT} --disable-pulseaudio --disable-esd --disable-haptic --disable-video-wayland --disable-video-vulkan || exit 1
	make -j9 && make install || exit 1
fi

# build qemu
if test "${BUILD_QEMU}" = "1" -o "${BUILD_ALL}" = "1"
then
	cd "${BD_BUILDIR}"
	# must delete all privious files if build staticly
	tar xJvf "${BD_DLDIR}/qemu-5.2.0.tar.xz"
	cd qemu-5.2.0
	# --audio-drv-list=sdl 
	./configure --prefix=${BD_SYSROOT} --target-list=x86_64-softmmu --disable-docs --disable-gtk --enable-avx2 --enable-sdl --enable-kvm --enable-spice --enable-usb-redir --enable-opengl || exit 1
	make -j9 && make install || exit 1
fi

# build virt-viewer
if test "${BUILD_VIEWER}" = "1" -o "${BUILD_ALL}" = "1"
then
	cd "${BD_BUILDIR}"
	tar xzvf "${BD_DLDIR}/virt-viewer-9.0.tar.gz"
	cd virt-viewer-9.0
	./configure --prefix=${BD_SYSROOT} --without-libvirt --without-gtk-vnc --without-vte --without-ovirt
	make -j9 && make install || exit 1
fi

