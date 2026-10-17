#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SDKROOT=${DOVEOS_SDK_SYSROOT:-/home/hakami55/doveos-gtk-sdk/sysroot}
LIBROOT=${DOVEOS_RUNTIME:-/home/hakami55/doveos-v2-build/desktop-extracted}
CC=${DOVEOS_CC:-/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc}
export PKG_CONFIG_LIBDIR="$SDKROOT/usr/lib/arm-linux-gnueabihf/pkgconfig:$SDKROOT/usr/share/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$SDKROOT"
export LD_LIBRARY_PATH=/opt/lapdock-focal-cross/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
source=${1:-$HERE/../settings_gtk.c}
output=${2:-$HERE/../artifacts/doveos-settings-ui}
if [ "$#" -ge 2 ]; then shift 2; else set --; fi
# Additional arguments are passed directly to the compiler (e.g. -pthread).
# Always use the current shared SDK, even when compiling older application code.
test "$(sed -n 's/^#define DOVEOS_SDK_VERSION_MAJOR //p' "$HERE/doveos.h" | tr -d '\r')" = 2
test -f "$source" || { echo "Source not found: $source" >&2; exit 2; }
mkdir -p "$(dirname -- "$output")"
cflags=$(pkg-config --cflags gtk+-2.0)
libs=$(pkg-config --libs gtk+-2.0)
# Settings uses asynchronous, read-only BlueZ status queries.
case "$source" in settings_gtk.c|*/settings_gtk.c) libs="$libs $(pkg-config --libs gio-2.0)";; esac
# GTK2 itself marks a few header declarations deprecated with newer GCC.
"$CC" --sysroot=/opt/lapdock-focal-cross -std=c11 -O2 -Wall -Wextra -Werror -Wno-deprecated-declarations \
 $cflags -I"$HERE" -I"$LIBROOT/usr/include" "$source" \
 -L"$LIBROOT/usr/lib/arm-linux-gnueabihf" -L"$LIBROOT/lib/arm-linux-gnueabihf" \
 -Wl,-rpath-link,"$LIBROOT/usr/lib/arm-linux-gnueabihf:$LIBROOT/lib/arm-linux-gnueabihf" \
 $libs -lX11 "$@" -o "$output"
file "$output"
