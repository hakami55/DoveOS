#!/bin/sh
set -eu
B=/mnt/c/vscode/HPlapdock/hdmi_input_study
S=/home/hakami55/doveos-uvc-20261005/libuvc
O=/home/hakami55/doveos-uvc-20261005/build
export PKG_CONFIG_LIBDIR=/usr/lib/arm-linux-gnueabihf/pkgconfig:/usr/share/pkgconfig
if [ "${DOVEOS_UI_ONLY:-0}" != 1 ];then
python3 "$B/patch-libuvc-preview.py"
cmake -S "$S" -B "$O" -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=arm -DCMAKE_C_COMPILER=arm-linux-gnueabihf-gcc -DCMAKE_BUILD_TYPE=Release -DCMAKE_BUILD_TARGET=Static -DBUILD_EXAMPLE=OFF -DBUILD_TEST=OFF -DBUILD_TESTING=OFF -DLIBUVC_NUM_TRANSFER_BUFS=32 -DLIBUVC_PACKETS_PER_TRANSFER_MAX=8 -DLibUSB_LIBRARY=/usr/lib/arm-linux-gnueabihf/libusb-1.0.so -DJPEG_LIBRARY=/usr/lib/arm-linux-gnueabihf/libjpeg.so
cmake --build "$O" -j2
git -C "$S" rev-parse HEAD > "$B/libuvc-commit.txt"
cp "$S/LICENSE.txt" "$B/LICENSE-libuvc.txt"
fi
SDKROOT=/home/hakami55/doveos-gtk-sdk/sysroot
D=/home/hakami55/doveos-v2-build/desktop-extracted
export PKG_CONFIG_LIBDIR="$SDKROOT/usr/lib/arm-linux-gnueabihf/pkgconfig:$SDKROOT/usr/share/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$SDKROOT"
export LD_LIBRARY_PATH=/opt/lapdock-focal-cross/usr/lib/x86_64-linux-gnu
/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc --sysroot=/opt/lapdock-focal-cross -std=c11 -O2 -Wall -Wextra -Wno-deprecated-declarations $(pkg-config --cflags gtk+-2.0) -I"$S/include" -I"$O/include" -I/mnt/c/vscode/HPlapdock/doveos_v2/sdk "$B/usb-video-ui.c" "$O/libuvc.a" -L"$D/usr/lib/arm-linux-gnueabihf" -L"$D/lib/arm-linux-gnueabihf" -Wl,-rpath-link,"$D/usr/lib/arm-linux-gnueabihf:$D/lib/arm-linux-gnueabihf:/usr/lib/arm-linux-gnueabihf" $(pkg-config --libs gtk+-2.0) /usr/lib/arm-linux-gnueabihf/libusb-1.0.so /usr/lib/arm-linux-gnueabihf/libjpeg.so -pthread -o "$B/usb-video-ui"
