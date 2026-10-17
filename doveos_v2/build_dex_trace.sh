#!/bin/sh
set -eu
HERE=/mnt/c/vscode/HPlapdock/doveos_v2
CC=/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc
export LD_LIBRARY_PATH=/opt/lapdock-focal-cross/usr/lib/x86_64-linux-gnu
"$CC" --sysroot=/opt/lapdock-focal-cross -std=c11 -O2 -Wall -Wextra -Werror "$HERE/dex_input.c" -ldl -lpthread -o "$HERE/artifacts/dex-input-pacing-probe"
"$CC" --sysroot=/opt/lapdock-focal-cross -std=c11 -O2 -Wall -Wextra -Werror "$HERE/dex_packet_timing.c" -o "$HERE/artifacts/dex-packet-timing"
