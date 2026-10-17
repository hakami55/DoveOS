#!/bin/sh
set -eu
P=/mnt/c/vscode/HPlapdock/doveos_v2/moonlight-ui
B=/mnt/c/vscode/HPlapdock/moonlight_dock_prototype
arm-linux-gnueabihf-gcc -O2 -Wall -Wextra -Werror -I"$B/src/libgamestream" -I"$B/src/third_party/moonlight-common-c/src" "$P/host-control.c" -L"$B/build-armhf/libgamestream" -lgamestream -o "$P/host-control"
