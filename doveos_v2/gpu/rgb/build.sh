#!/bin/sh
set -eu
Q=/mnt/c/vscode/HPlapdock/doveos_v2/gpu/rgb
D=/home/hakami55/doveos-v2-build/desktop-extracted
export LD_LIBRARY_PATH=/opt/lapdock-focal-cross/usr/lib/x86_64-linux-gnu
T=/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf
sh "$Q/xephyr/build-server.sh"
cp "$Q/xephyr/Xephyr-rgb-720" "$Q/Xephyr-rgb-720"
"$T-gcc" --sysroot=/opt/lapdock-focal-cross -O2 -std=c11 -Wall -Wextra -Wno-misleading-indentation -I"$D/usr/include" "$Q/rgb-x-guardian.c" -Wl,--no-as-needed -lpthread -ldl -rdynamic -o "$Q/rgb-x-guardian"
"$T-gcc" --sysroot=/opt/lapdock-focal-cross -O2 -std=c11 -fPIC -shared -Wall -Wextra -Wno-misleading-indentation -I/home/hakami55/doveos-v2-build/gpu-mali-r4/include -I"$D/usr/include" -I"$D/usr/include/arm-linux-gnueabihf" "$Q/rgb-present.c" -L"$D/usr/lib/arm-linux-gnueabihf" -L/mnt/c/vscode/HPlapdock/analysis/gpu-integrate-20261003 -Wl,-rpath-link,"$D/usr/lib/arm-linux-gnueabihf" -Wl,--no-as-needed -l:libMali-r4p1-native-trial.so -ldl -o "$Q/libgpu-rgb-present.so"
"$T-gcc" --sysroot=/opt/lapdock-focal-cross -O2 -std=c11 -Wall -Wextra -Wno-misleading-indentation -I"$D/usr/include" -I/mnt/c/vscode/HPlapdock/doveos_v2/cursors "$Q/rgb-cursor.c" -L"$D/usr/lib/arm-linux-gnueabihf" -Wl,-rpath-link,"$D/usr/lib/arm-linux-gnueabihf:$D/lib/arm-linux-gnueabihf" -lX11 -lXfixes -lXtst -lX11-xcb -lxcb -o "$Q/rgb-720-cursor"
sh "$Q/../native-window/build.sh"
"$T-gcc" --sysroot=/opt/lapdock-focal-cross -O2 -std=gnu17 -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -fPIC -shared -Wall -Wextra -Werror -Wno-misleading-indentation -Wno-unused-function -I/home/hakami55/doveos-v2-build/gpu-mali-r4/include -I"$D/usr/include" "$Q/bridge-compositor.c" -L"$D/usr/lib/arm-linux-gnueabihf" -Wl,-rpath-link,"$D/usr/lib/arm-linux-gnueabihf" -lX11 -lXext -ldl -o "$Q/libdove-compositor-bridge.so"
cd "$Q"
sha256sum rgb-x-guardian libgpu-rgb-present.so rgb-720-cursor Xephyr-rgb-720 libdove-compositor-bridge.so > files.sha256
tar -czf runtime.tar.gz rgb-x-guardian libgpu-rgb-present.so rgb-720-cursor Xephyr-rgb-720 libdove-compositor-bridge.so files.sha256
sha256sum runtime.tar.gz > runtime.sha256
