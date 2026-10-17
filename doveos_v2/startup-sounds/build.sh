#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SDK=/home/hakami55/doveos-gtk-sdk/sysroot
LIB=/home/hakami55/doveos-v2-build/desktop-extracted
CC=/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc
export LD_LIBRARY_PATH=/opt/lapdock-focal-cross/usr/lib/x86_64-linux-gnu
export PKG_CONFIG_LIBDIR="$SDK/usr/lib/arm-linux-gnueabihf/pkgconfig:$SDK/usr/share/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$SDK"
"$CC" --sysroot=/opt/lapdock-focal-cross -O2 -Wall -Wextra -Werror "$HERE/scale-wave.c" -o "$HERE/scale-wave"
"$CC" --sysroot=/opt/lapdock-focal-cross -O2 -Wall -Wextra -Werror -Wno-deprecated-declarations -shared -fPIC $(pkg-config --cflags gtk+-2.0) "$HERE/system-sounds-gtk.c" -L"$LIB/usr/lib/arm-linux-gnueabihf" -Wl,-rpath-link,"$LIB/usr/lib/arm-linux-gnueabihf:$LIB/lib/arm-linux-gnueabihf" $(pkg-config --libs gtk+-2.0) -o "$HERE/system-sounds-gtk.so"
sh -n "$HERE/system-sounds.sh"
sh "$HERE/../sdk/build-app.sh"
