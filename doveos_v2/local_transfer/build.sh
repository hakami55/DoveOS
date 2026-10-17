#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SDKROOT=${DOVEOS_SDK_SYSROOT:-/home/hakami55/doveos-gtk-sdk/sysroot}
LIBROOT=${DOVEOS_RUNTIME:-/home/hakami55/doveos-v2-build/desktop-extracted}
CC=${DOVEOS_CC:-/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc}
export PKG_CONFIG_LIBDIR="$SDKROOT/usr/lib/arm-linux-gnueabihf/pkgconfig:$SDKROOT/usr/share/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$SDKROOT"
export LD_LIBRARY_PATH=/opt/lapdock-focal-cross/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
"$CC" --sysroot=/opt/lapdock-focal-cross -std=c11 -O2 -Wall -Wextra -Werror -Wno-deprecated-declarations -Wno-misleading-indentation \
 $(pkg-config --cflags gtk+-2.0 gio-2.0 gio-unix-2.0) -I"$HERE/../sdk" -I"$LIBROOT/usr/include" "$HERE/transfer.c" "$HERE/qr/qrcodegen.c" \
 -L"$LIBROOT/usr/lib/arm-linux-gnueabihf" -L"$LIBROOT/lib/arm-linux-gnueabihf" \
 -Wl,-rpath-link,"$LIBROOT/usr/lib/arm-linux-gnueabihf:$LIBROOT/lib/arm-linux-gnueabihf" \
 $(pkg-config --libs gtk+-2.0 gio-2.0 gio-unix-2.0) -lX11 -o "$HERE/doveos-transfer"
file "$HERE/doveos-transfer"
