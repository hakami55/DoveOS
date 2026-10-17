#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2/DovePlayer
R=/tmp/doveplayer-runtime
Q=/tmp/doveplayer
mkdir -p "$Q" "$R"
if ! grep -q " $R " /proc/mounts;then
 (cd "$B";sha256sum -c runtime.sha256) >/dev/null
 mount -o loop,ro "$B/runtime.ext2" "$R"
fi
test -f "$R/lib/libavformat.so.58"
