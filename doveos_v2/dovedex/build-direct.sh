set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export LD_LIBRARY_PATH=/opt/lapdock-focal-cross/usr/lib/x86_64-linux-gnu
/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc --sysroot=/opt/lapdock-focal-cross -O2 -Wall -Wextra -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -I/mnt/c/vscode/HPlapdock/hp_lapdock_usb_research/wireless_updater_20260927/decoder_probe/include -I"$P" "$P/dovedex-direct-host.c" -lpthread -ldl -rdynamic -o "$P/dovedex-direct-host"
sh /mnt/c/vscode/HPlapdock/doveos_v2/dovedex/build-viewer.sh "$P/dovedex-viewer.c" "$P/dovedex-viewer"
