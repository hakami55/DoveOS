#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DEPS=/home/hakami55/doveos-v2-build/desktop-extracted
CC=/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc
"$CC" --sysroot=/opt/lapdock-focal-cross -std=c11 -O2 -Wall -Wextra -Werror \
 -I"$DEPS/usr/include" "$HERE/dex_ui.c" \
 -L"$DEPS/usr/lib/arm-linux-gnueabihf" -L"$DEPS/lib/arm-linux-gnueabihf" \
 -Wl,-rpath-link,"$DEPS/usr/lib/arm-linux-gnueabihf:$DEPS/lib/arm-linux-gnueabihf" \
 -lX11 -l:libXft.so.2.3.3 -o "$HERE/artifacts/doveos-dex"
"$CC" --sysroot=/opt/lapdock-focal-cross -std=c11 -O2 -Wall -Wextra -Werror \
 "$HERE/moonlight_quit_combo.c" -o "$HERE/artifacts/moonlight-quit-combo"
"$CC" --sysroot=/opt/lapdock-focal-cross -std=c11 -O2 -Wall -Wextra -Werror \
 "$HERE/dex_input.c" -ldl -lpthread -o "$HERE/artifacts/dex-input"
"$CC" --sysroot=/opt/lapdock-focal-cross -std=c11 -O2 -Wall -Wextra -Werror \
 "$HERE/dex_keyboard_route.c" -o "$HERE/artifacts/dex-keyboard-route"
cp "$HERE/artifacts/doveos-dex" "$HERE/overlay/usr/bin/doveos-dex"
cp "$HERE/start-desktop.sh" "$HERE/artifacts/start-desktop.sh"
cp "$HERE/dex-v2-host.sh" "$HERE/artifacts/dex-v2-host.sh"
file "$HERE/artifacts/doveos-dex" "$HERE/artifacts/moonlight-quit-combo"
