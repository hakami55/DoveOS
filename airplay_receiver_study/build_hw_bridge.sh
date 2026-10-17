#!/bin/sh
set -eu
BASE=/mnt/c/vscode/HPlapdock
INC="$BASE/hp_lapdock_usb_research/wireless_updater_20260927/decoder_probe/include"
export PKG_CONFIG_LIBDIR=/usr/lib/arm-linux-gnueabihf/pkgconfig:/usr/share/pkgconfig
arm-linux-gnueabihf-gcc -O2 -Wall -Wextra -Wno-unused-function \
  -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard \
  -I"$INC" -I"$BASE/moonlight_dock_prototype/src/third_party/h264bitstream" \
  -I"$BASE/moonlight_dock_prototype/src/third_party/moonlight-common-c/src" \
  -I"$BASE/moonlight_dock_prototype/src/libgamestream" \
  $(pkg-config --cflags gstreamer-app-1.0) \
  "$BASE/airplay_receiver_study/airplay_hw_bridge.c" \
  "$BASE/moonlight_dock_prototype/src/third_party/h264bitstream/h264_stream.c" \
  "$BASE/moonlight_dock_prototype/src/third_party/h264bitstream/h264_nal.c" \
  "$BASE/moonlight_dock_prototype/src/third_party/h264bitstream/h264_sei.c" \
  $(pkg-config --libs gstreamer-app-1.0) \
  -lm -lpthread -ldl -rdynamic -o "$BASE/airplay_receiver_study/airplay-hw-bridge-arm"
