#!/bin/sh
# Bind a verified replacement over one confirmed damaged read-only image file.
# The base image remains intact; normal desktop cleanup unmounts this overlay.
set -eu
M=/tmp/doveos-v2-root
B=/media/usb0/DoveOS-v2/RuntimeFixes
Q=/tmp/doveos-runtime-fixes
target=$M/lib/arm-linux-gnueabihf/libz.so.1.2.11
good=3c8d327a895fdd7b40dfe97295a2d69c8218199630fa0e2b898ebf00dc890d64
bad=a022d2df3ffcc7e658bac6a33b466a5e7814eb0b643e232bad1b2b8c249e5f84
test -f "$target" && test ! -L "$target"
current=$(sha256sum "$target" | cut -d ' ' -f1)
if [ "$current" = "$good" ];then echo RUNTIME_ZLIB_ALREADY_VALID;exit 0;fi
test "$current" = "$bad" || { echo RUNTIME_ZLIB_UNRECOGNIZED_COPY_NOT_CHANGED;exit 3; }
test "$(sha256sum "$B/libz.so.1" | cut -d ' ' -f1)" = "$good"
test ! -L "$Q"
mkdir -p "$Q"
cp "$B/libz.so.1" "$Q/libz.so.1.next"
test "$(sha256sum "$Q/libz.so.1.next" | cut -d ' ' -f1)" = "$good"
mv "$Q/libz.so.1.next" "$Q/libz.so.1"
mount --bind "$Q/libz.so.1" "$target"
if [ "$(sha256sum "$target" | cut -d ' ' -f1)" != "$good" ];then umount "$target";exit 4;fi
echo RUNTIME_ZLIB_VERIFIED_OVERLAY_ACTIVE
