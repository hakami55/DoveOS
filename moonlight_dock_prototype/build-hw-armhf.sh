#!/bin/sh
set -eu
BASE=/mnt/c/vscode/HPlapdock/moonlight_dock_prototype
INC=/mnt/c/vscode/HPlapdock/hp_lapdock_usb_research/wireless_updater_20260927/decoder_probe/include
python3 "$BASE/build_hw_client.py" "$@"
arm-linux-gnueabihf-gcc -DVIEW -DPLAY_AUDIO -DDOCK_INPUT -O2 -Wall -Wextra -Werror \
  -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard \
  -I/usr/include/opus -I"$INC" -I"$BASE/src/libgamestream" -I"$BASE/src/third_party/h264bitstream" \
  -I"$BASE/src/third_party/moonlight-common-c/src" "$BASE/stream_hw.c" \
  -L"$BASE/build-armhf/libgamestream" -lgamestream -lmoonlight-common \
  -lm -lpthread -lopus -ldl -rdynamic -o "$BASE/dock-stream-hw"
arm-linux-gnueabihf-gcc -O2 -Wall -Wextra -Werror "$BASE/hw_trial_launcher.c" -o "$BASE/hw-trial-launcher"
