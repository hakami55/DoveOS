#!/bin/sh
set -eu
Q=/mnt/c/vscode/HPlapdock/doveos_v2/gpu/native-window
D=/home/hakami55/doveos-v2-build/desktop-extracted
export LD_LIBRARY_PATH=/opt/lapdock-focal-cross/usr/lib/x86_64-linux-gnu
C=/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc
F="--sysroot=/opt/lapdock-focal-cross -O2 -std=gnu17 -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -Wall -Wextra -Werror -Wno-misleading-indentation -Wno-unused-function"
INC="-I/home/hakami55/doveos-v2-build/gpu-mali-r4/include -I$D/usr/include"
LIB="-L$D/usr/lib/arm-linux-gnueabihf -L/mnt/c/vscode/HPlapdock/analysis/gpu-integrate-20261003 -Wl,-rpath-link,$D/usr/lib/arm-linux-gnueabihf"
$C $F $INC -fPIC -shared "$Q/gpu-window-api.c" $LIB -l:libMali-r4p1-native-trial.so -lX11 -lXext -ldl -lpthread -o "$Q/libdove-gpu-window.so"
$C $F $INC "$Q/gpu-demo.c" $LIB -L"$Q" -ldove-gpu-window -l:libMali-r4p1-native-trial.so -lX11 -o "$Q/dove-gpu-demo"
sh "$Q/../../sdk/build-app.sh" "$Q/../../sdk/examples/mouse-test-gpu.c" "$Q/doveos-mouse-test-gpu" \
 -Wno-misleading-indentation -I"$Q" -I/home/hakami55/doveos-v2-build/gpu-mali-r4/include \
 -L"$Q" -L/mnt/c/vscode/HPlapdock/analysis/gpu-integrate-20261003 -ldove-gpu-window -l:libMali-r4p1-native-trial.so
cd "$Q"
sha256sum libdove-gpu-window.so dove-gpu-demo doveos-mouse-test-gpu dove_gpu_window.h > files.sha256
tar -czf runtime.tar.gz libdove-gpu-window.so dove-gpu-demo doveos-mouse-test-gpu dove_gpu_window.h files.sha256
sha256sum runtime.tar.gz > runtime.sha256
