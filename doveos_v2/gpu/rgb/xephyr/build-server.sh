#!/bin/sh
set -eu
Q=/mnt/c/vscode/HPlapdock/doveos_v2/gpu/rgb/xephyr
B=/home/hakami55/doveos-v2-build/xephyr-source/build
export LD_LIBRARY_PATH=/opt/lapdock-focal-cross/usr/lib/x86_64-linux-gnu
python3 "$Q/prepare-copy.py"
cd "$B/glamor"
make -f "$Q/copy.mk" "$Q/glamor_copy.o" > "$Q/build-copy.log" 2>&1 || { tail -20 "$Q/build-copy.log";exit 1; }
/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-ar r "$Q/.libs/libglamor.a" "$Q/glamor_copy.o"
cd "$B/hw/kdrive/ephyr"
make -f "$Q/server.mk" "$Q/Xephyr-rgb-720" > "$Q/build-server.log" 2>&1 || { tail -20 "$Q/build-server.log";exit 1; }
sha256sum "$Q/Xephyr-rgb-720"
