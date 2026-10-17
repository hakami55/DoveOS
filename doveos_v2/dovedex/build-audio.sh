#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export LD_LIBRARY_PATH=/opt/lapdock-focal-cross/usr/lib/x86_64-linux-gnu
CC=/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc
"$CC" --sysroot=/opt/lapdock-focal-cross -O2 -static -Wall -Wextra -Werror "$P/dovedex-pcm.c" -o "$P/dovedex-pcm"
sh "$P/build-viewer.sh" "$P/dovedex-viewer.c" "$P/dovedex-viewer"
sh "$P/build-viewer.sh" "$P/dovedex.c" "$P/dovedex"
