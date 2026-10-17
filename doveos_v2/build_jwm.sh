#!/bin/sh
set -e
cd /home/hakami55/doveos-jwm/jwm-2.3.7
sh autogen.sh >/tmp/doveos-jwm-autogen.log 2>&1
export LD_LIBRARY_PATH=/opt/lapdock-focal-cross/usr/lib/x86_64-linux-gnu
SDK=/home/hakami55/doveos-gtk-sdk/sysroot
export PKG_CONFIG_SYSROOT_DIR=$SDK
export PKG_CONFIG_LIBDIR=$SDK/usr/lib/arm-linux-gnueabihf/pkgconfig:$SDK/usr/share/pkgconfig
export CC=/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc
export CFLAGS="--sysroot=/opt/lapdock-focal-cross -O2 -I$SDK/usr/include -I$SDK/usr/include/freetype2"
export LDFLAGS="-L$SDK/usr/lib/arm-linux-gnueabihf -Wl,-rpath-link,$SDK/usr/lib/arm-linux-gnueabihf:$SDK/lib/arm-linux-gnueabihf:/home/hakami55/doveos-v2-build/desktop-extracted/lib/arm-linux-gnueabihf:/home/hakami55/doveos-v2-build/desktop-extracted/usr/lib/arm-linux-gnueabihf"
./configure --host=arm-linux-gnueabihf --disable-rsvg --disable-cairo --disable-jpeg --disable-xpm --disable-xinerama >/tmp/doveos-jwm-config.log 2>&1
make -j4 >/tmp/doveos-jwm-build.log 2>&1
cp src/jwm /mnt/c/vscode/HPlapdock/doveos_v2/artifacts/jwm
