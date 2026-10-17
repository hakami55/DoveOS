set -eu
P=/mnt/c/vscode/HPlapdock/analysis/dovedex-20261003
export LD_LIBRARY_PATH=/opt/lapdock-focal-cross/usr/lib/x86_64-linux-gnu
/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc --sysroot=/opt/lapdock-focal-cross -O2 -Wall -Wextra -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -I/mnt/c/vscode/HPlapdock/hp_lapdock_usb_research/wireless_updater_20260927/decoder_probe/include -I"$P" "$P/dovedex-rtk-host.c" -lpthread -ldl -rdynamic -o "$P/dovedex-rtk-host"
