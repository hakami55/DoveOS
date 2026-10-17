#!/bin/sh
set -eu
H=/mnt/c/vscode/HPlapdock/doveos_v2
D=/home/hakami55/doveos-v2-build/desktop-extracted
/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc \
 --sysroot=/opt/lapdock-focal-cross -O2 -I"$D/usr/include" \
 "$H/mouse-test-check.c" -L"$D/usr/lib/arm-linux-gnueabihf" \
 -Wl,-rpath-link,"$D/usr/lib/arm-linux-gnueabihf:$D/lib/arm-linux-gnueabihf" \
 -Wl,--disable-new-dtags,-rpath,/tmp/doveos-v2-root/usr/lib/arm-linux-gnueabihf:/tmp/doveos-v2-root/lib/arm-linux-gnueabihf -lX11 -lXtst -o "$H/artifacts/menu-check"
