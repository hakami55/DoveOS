#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
export LD_LIBRARY_PATH=/opt/lapdock-focal-cross/usr/lib/x86_64-linux-gnu
CC=/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc
sh "$P/sdk/build-app.sh"
"$CC" --sysroot=/opt/lapdock-focal-cross -std=c11 -O2 -Wall -Wextra -Werror -static "$P/power/power-card.c" -o "$P/power/power-card"
"$CC" --sysroot=/opt/lapdock-focal-cross -std=c11 -O2 -Wall -Wextra -Werror "$P/power_manager.c" -o "$P/artifacts/power-manager"
sh -n "$P/power/host.sh"
sh -n "$P/power/unmount-system.sh"
(cd "$P/power";sha256sum host.sh unmount-system.sh power-card > files.sha256)
echo POWER_BUILD_PASSED
