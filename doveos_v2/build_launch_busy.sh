#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DEPS=/home/hakami55/doveos-v2-build/desktop-extracted
/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc --sysroot=/opt/lapdock-focal-cross \
 -std=c11 -O2 -Wall -Wextra -Werror -I"$DEPS/usr/include" "$HERE/launch_busy.c" \
 -L"$DEPS/usr/lib/arm-linux-gnueabihf" -L"$DEPS/lib/arm-linux-gnueabihf" \
 -Wl,-rpath-link,"$DEPS/usr/lib/arm-linux-gnueabihf:$DEPS/lib/arm-linux-gnueabihf" \
 -lX11 -o "$HERE/overlay/usr/bin/doveos-launch-busy"
