#!/bin/sh
set -eu
B=/mnt/c/vscode/HPlapdock/hdmi_input_study
S=/home/hakami55/doveos-uvc-20261005/libuvc
O=/home/hakami55/doveos-uvc-20261005/build
SDKROOT=/home/hakami55/doveos-gtk-sdk/sysroot
D=/home/hakami55/doveos-v2-build/desktop-extracted
export PKG_CONFIG_LIBDIR="$SDKROOT/usr/lib/arm-linux-gnueabihf/pkgconfig:$SDKROOT/usr/share/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$SDKROOT"
export LD_LIBRARY_PATH=/opt/lapdock-focal-cross/usr/lib/x86_64-linux-gnu
source=tv-ui.c
target=tv-ui
if [ "${1:-}" = test ];then source=hdmi-test-ui.c;target=hdmi-test-ui;fi
/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc --sysroot=/opt/lapdock-focal-cross -std=c11 -O2 -Wall -Wextra -Wno-deprecated-declarations -Wno-misleading-indentation $(pkg-config --cflags gtk+-2.0) -I"$S/include" -I"$O/include" -I/mnt/c/vscode/HPlapdock/doveos_v2/sdk "$B/$source" "$O/libuvc.a" -L"$D/usr/lib/arm-linux-gnueabihf" -L"$D/lib/arm-linux-gnueabihf" -Wl,-rpath-link,"$D/usr/lib/arm-linux-gnueabihf:$D/lib/arm-linux-gnueabihf:/usr/lib/arm-linux-gnueabihf" $(pkg-config --libs gtk+-2.0) /usr/lib/arm-linux-gnueabihf/libusb-1.0.so /usr/lib/arm-linux-gnueabihf/libjpeg.so -pthread -o "$B/$target"
