#!/bin/sh
set -eu
Q=/mnt/c/vscode/HPlapdock/doveos_v2/gpu/direct-apps
D=/home/hakami55/doveos-v2-build/desktop-extracted
export LD_LIBRARY_PATH=/opt/lapdock-focal-cross/usr/lib/x86_64-linux-gnu
C=/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc
"$C" --sysroot=/opt/lapdock-focal-cross -O2 -std=c11 -Wall -Wextra -Werror -fPIC -shared -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -I/home/hakami55/doveos-v2-build/gpu-mali-r4/include -I"$D/usr/include" "$Q/egl-direct-apps.c" -L"$D/usr/lib/arm-linux-gnueabihf" -Wl,-rpath-link,"$D/usr/lib/arm-linux-gnueabihf:$D/lib/arm-linux-gnueabihf" -lX11 -lXext -ldl -lpthread -Wl,-soname,libEGL.so.1 -o "$Q/libEGL.so.1"
cp "$Q/../native-window/libdove-gpu-window.so" "$Q/libdove-gpu-window.so"
cd "$Q"
sha256sum libEGL.so.1 libdove-gpu-window.so > files.sha256
tar -czf runtime.tar.gz libEGL.so.1 libdove-gpu-window.so files.sha256
sha256sum runtime.tar.gz > runtime.sha256
