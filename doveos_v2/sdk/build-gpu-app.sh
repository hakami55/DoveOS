#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
GPU=${DOVEOS_GPU_SDK:-$HERE/gpu}
CC=${DOVEOS_CC:-/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc}
LIBROOT=${DOVEOS_RUNTIME:-/home/hakami55/doveos-v2-build/desktop-extracted}
export LD_LIBRARY_PATH=/opt/lapdock-focal-cross/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
test "$#" -eq 2
"$CC" --sysroot=/opt/lapdock-focal-cross -std=c11 -O2 -Wall -Wextra \
 -I"$GPU/include" -I"$LIBROOT/usr/include" "$1" \
 -L"$GPU/lib" -Wl,-rpath-link,"$LIBROOT/usr/lib/arm-linux-gnueabihf:$LIBROOT/lib/arm-linux-gnueabihf" \
 -Wl,--no-as-needed -lMali -lpthread -ldl -o "$2"
file "$2"
