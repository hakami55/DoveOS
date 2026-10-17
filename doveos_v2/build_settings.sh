#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DEPS=/home/hakami55/doveos-v2-build/desktop-extracted
CC=/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc
 /bin/sh "$HERE/sdk/build-app.sh"
"$CC" --sysroot=/opt/lapdock-focal-cross -std=c11 -O2 -Wall -Wextra -Werror \
 -I"$DEPS/usr/include" -I/home/hakami55/doveos-gtk-sdk/sysroot/usr/include -I/home/hakami55/doveos-gtk-sdk/sysroot/usr/include/freetype2 "$HERE/status_tray.c" \
 -L"$DEPS/usr/lib/arm-linux-gnueabihf" -L"$DEPS/lib/arm-linux-gnueabihf" \
 -Wl,-rpath-link,"$DEPS/usr/lib/arm-linux-gnueabihf:$DEPS/lib/arm-linux-gnueabihf" \
 -lX11 -lXext -l:libXft.so.2.3.3 -o "$HERE/artifacts/doveos-status"
cp "$HERE/artifacts/doveos-settings-ui" "$HERE/overlay/usr/bin/doveos-settings-ui"
cp "$HERE/artifacts/doveos-status" "$HERE/overlay/usr/bin/doveos-status"
file "$HERE/artifacts/doveos-settings-ui" "$HERE/artifacts/doveos-status"
