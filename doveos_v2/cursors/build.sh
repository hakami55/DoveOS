#!/bin/sh
set -eu
D=/home/hakami55/doveos-v2-build/desktop-extracted
P=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir -p "$P/package"
export LD_LIBRARY_PATH=/opt/lapdock-focal-cross/usr/lib/x86_64-linux-gnu
CC=/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc
$CC --sysroot=/opt/lapdock-focal-cross -O2 -I"$D/usr/include" "$P/gpu-cursor.c" -L"$D/usr/lib/arm-linux-gnueabihf" -Wl,-rpath-link,"$D/usr/lib/arm-linux-gnueabihf:$D/lib/arm-linux-gnueabihf" -lX11 -lXfixes -lXtst -ldl -pthread -o "$P/package/gpu-cursor"
$CC --sysroot=/opt/lapdock-focal-cross -O2 -Wall -Wextra -I"$D/usr/include" "$P/open_app.c" -L"$D/usr/lib/arm-linux-gnueabihf" -Wl,-rpath-link,"$D/usr/lib/arm-linux-gnueabihf:$D/lib/arm-linux-gnueabihf" -lX11 -o "$P/package/doveos-open"

$CC --sysroot=/opt/lapdock-focal-cross -O2 -I"$D/usr/include" "$P/probe.c" -L"$D/usr/lib/arm-linux-gnueabihf" -Wl,-rpath-link,"$D/usr/lib/arm-linux-gnueabihf:$D/lib/arm-linux-gnueabihf" -lX11 -lXfixes -o "$P/probe"
