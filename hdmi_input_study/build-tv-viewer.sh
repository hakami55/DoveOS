#!/bin/sh
set -eu
B=/mnt/c/vscode/HPlapdock
S=/home/hakami55/doveos-uvc-20261005/libuvc
O=/home/hakami55/doveos-uvc-20261005/build
export LD_LIBRARY_PATH=/opt/lapdock-focal-cross/usr/lib/x86_64-linux-gnu
resolution_flags=
target=tv-viewer
if [ "${1:-}" = 1080 ];then resolution_flags='-DTV_CAPTURE_WIDTH=1920 -DTV_CAPTURE_HEIGHT=1080';target=tv-viewer-1080;fi
/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc --sysroot=/opt/lapdock-focal-cross -std=c11 -O3 -Wall -Wextra -Wno-misleading-indentation $resolution_flags -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -I"$B/hp_lapdock_usb_research/wireless_updater_20260927/decoder_probe/include" -I"$S/include" -I"$O/include" -I/usr/include/arm-linux-gnueabihf -idirafter /usr/include -I"$B/usb_desktop_poc/third_party/libyuv/include" "$B/hdmi_input_study/tv-viewer.c" "$O/libuvc.a" "$B/usb_desktop_poc/third_party/libyuv/libyuv.a" /usr/lib/arm-linux-gnueabihf/libjpeg.so /usr/lib/arm-linux-gnueabihf/libusb-1.0.so -Wl,-rpath-link,/usr/lib/arm-linux-gnueabihf -pthread -ldl -Wl,--no-as-needed -lm -Wl,--as-needed -rdynamic -o "$B/hdmi_input_study/$target"
