#!/bin/bash
set -eu
cd /home/hakami55/dovedeX-v01/boringssl-source
rm -f debian/out/libcrypto.so
make -f debian/libcrypto.mk \
 CC=/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc \
 DEB_HOST_ARCH=none \
 CFLAGS='--sysroot=/opt/lapdock-focal-cross -O2 -fPIC -mno-unaligned-access -DOPENSSL_NO_ASM -D_XOPEN_SOURCE=700 -DBORINGSSL_ANDROID_SYSTEM -DBORINGSSL_IMPLEMENTATION -DBORINGSSL_SHARED_LIBRARY -DOPENSSL_SMALL -fvisibility=hidden' > build.log 2>&1
cp debian/out/libcrypto.so.0 /home/hakami55/dovedeX-v01/runtime/usr/lib/arm-linux-gnueabihf/android/libcrypto.so.0
cp debian/out/libcrypto.so.0 /mnt/c/vscode/HPlapdock/doveos_v2/artifacts/dovedeX-libcrypto.so.0
sha256sum debian/out/libcrypto.so.0
