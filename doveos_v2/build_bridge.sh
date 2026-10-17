#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DEPS=/home/hakami55/doveos-v2-build/desktop-extracted
CC=/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc
"$CC" --sysroot=/opt/lapdock-focal-cross -std=c11 -O2 -Wall -Wextra -Werror \
  -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard \
  -I"$DEPS/usr/include" -I"$HERE/../usb_desktop_poc/third_party/libyuv/include" \
  ${BRIDGE_TRIAL_FLAGS:--DCLICKPAD_RAM_TRIAL} "$HERE/x11_bridge.c" "$HERE/../usb_desktop_poc/third_party/libyuv/libyuv.a" \
  -L"$DEPS/usr/lib/arm-linux-gnueabihf" -L"$DEPS/lib/arm-linux-gnueabihf" \
  -Wl,-rpath-link,"$DEPS/usr/lib/arm-linux-gnueabihf:$DEPS/lib/arm-linux-gnueabihf" \
  -lX11 -lXtst -lXfixes -lXdamage -lpthread -ldl -rdynamic -o "${BRIDGE_OUTPUT:-$HERE/artifacts/x11-bridge}"
