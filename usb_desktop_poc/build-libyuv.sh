#!/bin/sh
set -eu
cd /mnt/c/vscode/HPlapdock/usb_desktop_poc/third_party/libyuv
make -B -f linux.mk libyuv.a -j2 \
  CC=/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc \
  CXX=arm-linux-gnueabihf-g++ \
  AR=/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-ar \
  CFLAGS='-O3 -mfpu=neon-vfpv4 -mfloat-abi=hard -march=armv7-a -Iinclude/' \
  CXXFLAGS='-O3 -mfpu=neon-vfpv4 -mfloat-abi=hard -march=armv7-a -fno-exceptions -fno-rtti -Iinclude/' \
  > /tmp/drmosa-libyuv-build.log 2>&1
tail -3 /tmp/drmosa-libyuv-build.log
