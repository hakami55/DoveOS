#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DEPS=/home/hakami55/doveos-v2-build/desktop-extracted
CC=/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc
python3 "$HERE/make_moonlight_icon.py"
"$CC" --sysroot=/opt/lapdock-focal-cross -std=c11 -O2 -Wall -Wextra -Werror \
 -DDOVEOS_XWINDOW -I"$HERE" -I"$DEPS/usr/include" \
 "$HERE/../usb_desktop_poc/menu.c" \
 -L"$DEPS/usr/lib/arm-linux-gnueabihf" -L"$DEPS/lib/arm-linux-gnueabihf" \
 -Wl,-rpath-link,"$DEPS/usr/lib/arm-linux-gnueabihf:$DEPS/lib/arm-linux-gnueabihf" \
 -lX11 -l:libXft.so.2.3.3 -L"$HERE/runtime/usr/lib/arm-linux-gnueabihf" \
 -Wl,-rpath-link,"$HERE/runtime/usr/lib/arm-linux-gnueabihf" \
 -lpthread -ldl -rdynamic -o "$HERE/artifacts/doveos-control-x"
cp "$HERE/artifacts/doveos-control-x" "$HERE/overlay/usr/bin/doveos-control-x"
"$CC" --sysroot=/opt/lapdock-focal-cross -std=c11 -O2 -Wall -Wextra -Werror \
 "$HERE/moonlight_quit_combo.c" -o "$HERE/artifacts/moonlight-quit-combo"
