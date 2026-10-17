#!/bin/sh
set -eu
B=/mnt/c/vscode/HPlapdock
Q=$B/doveos_v2/doveplayer
D=/home/hakami55/doveos-v2-build/desktop-extracted
F=/home/hakami55/doveos-mpv-trial-20261007/bundle/lib
H=/home/hakami55/doveplayer-sdk/root/usr/include/arm-linux-gnueabihf
export LD_LIBRARY_PATH=/opt/lapdock-focal-cross/usr/lib/x86_64-linux-gnu
CC=/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc
CFLAGS='--sysroot=/opt/lapdock-focal-cross -O2 -std=gnu11 -Wall -Wextra -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard'
"$CC" $CFLAGS -I"$B/hp_lapdock_usb_research/wireless_updater_20260927/decoder_probe/include" "$Q/broker.c" -lpthread -ldl -rdynamic -o "$Q/doveplayer-broker" 2>"$Q/broker-build.log"
"$CC" $CFLAGS -Werror -I"$B/analysis/gpu-integration-20261007/include" "$Q/audio-host.c" -ldl -o "$Q/doveplayer-audio"
"$CC" $CFLAGS -Wno-deprecated-declarations -I"$H" "$Q/player.c" -L"$F" -Wl,-rpath-link,"$F" -l:libavformat.so.58 -l:libavcodec.so.58 -l:libavutil.so.56 -l:libswresample.so.3 -lpthread -lm -o "$Q/doveplayer-core"
if [ -f "$Q/ui.c" ];then sh "$B/doveos_v2/sdk/build-app.sh" "$Q/ui.c" "$Q/doveplayer-ui";fi
