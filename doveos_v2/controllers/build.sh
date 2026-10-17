#!/bin/sh
set -eu
cd /mnt/c/vscode/HPlapdock/doveos_v2/controllers
arm-linux-gnueabihf-gcc -std=c11 -O2 -Wall -Wextra -Werror -Wno-format-truncation -static xbox_usb.c -o doveos-xbox-usb
file doveos-xbox-usb
arm-linux-gnueabihf-gcc -std=c11 -O2 -Wall -Wextra -Werror -Wno-format-truncation -static ff_test.c -o ff-test
arm-linux-gnueabihf-gcc -std=c11 -O2 -Wall -Wextra -Werror -Wno-format-truncation -static test_protocol.c -o test-protocol
qemu-arm ./test-protocol
gcc -O2 -Wall -Wextra -Werror -Wno-unused-function -I../../moonlight_dock_prototype/src/third_party/moonlight-common-c/src test_moonlight.c -o test-moonlight
./test-moonlight
sh ../sdk/build-app.sh /mnt/c/vscode/HPlapdock/doveos_v2/controllers/controller_tester.c /mnt/c/vscode/HPlapdock/doveos_v2/controllers/doveos-controller-tester
