set -eu
P=/mnt/c/vscode/HPlapdock/analysis/dovedex-20261003
export LD_LIBRARY_PATH=/opt/lapdock-focal-cross/usr/lib/x86_64-linux-gnu
CC=/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc
"$CC" --sysroot=/opt/lapdock-focal-cross -static -std=c11 -O2 -Wall -Wextra -Werror "$P/dovedex-run.c" -o "$P/dovedex-run"
"$CC" --sysroot=/opt/lapdock-focal-cross -static -O2 -Wall -Wextra -Werror "$P/dovedex-quit.c" -o "$P/dovedex-quit"
