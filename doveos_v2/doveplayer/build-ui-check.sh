#!/bin/sh
set -eu
Q=/mnt/c/vscode/HPlapdock/doveos_v2/doveplayer
D=/home/hakami55/doveos-v2-build/desktop-extracted
export LD_LIBRARY_PATH=/opt/lapdock-focal-cross/usr/lib/x86_64-linux-gnu
/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc --sysroot=/opt/lapdock-focal-cross -O2 -Wall -Wextra -Werror -I"$D/usr/include" "$Q/ui-check-click.c" -L"$D/usr/lib/arm-linux-gnueabihf" -Wl,-rpath-link,"$D/usr/lib/arm-linux-gnueabihf:$D/lib/arm-linux-gnueabihf" -lX11 -ldl -o "$Q/ui-check-click"
