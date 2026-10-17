#!/bin/sh
set -eu
BASE=/mnt/c/vscode/HPlapdock/moonlight_dock_prototype
arm-linux-gnueabihf-gcc -O2 -Wall -Wextra -Werror \
  -I"$BASE/src/libgamestream" \
  -I"$BASE/src/third_party/moonlight-common-c/src" \
  "$BASE/pair_only.c" -L"$BASE/build-armhf/libgamestream" -lgamestream \
  -o "$BASE/dock-moonlight-pair"
arm-linux-gnueabihf-readelf -h "$BASE/dock-moonlight-pair" | grep 'Machine:'
