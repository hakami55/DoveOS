#!/bin/sh
# Run the desktop stored inside the separate Buildroot image, retaining the
# vendor host services and app ABI during this first integration prototype.
set -eu
MOUNT=/tmp/doveos-v2-root
IMAGE=/media/usb0/DoveOS-v2/doveos-v2.ext2
if ! grep -q " $MOUNT " /proc/mounts; then
    mkdir -p "$MOUNT"
    mount -t ext2 -o loop,ro "$IMAGE" "$MOUNT"
fi
test -x "$MOUNT/usr/bin/doveos-desktop"
exec /tmp/drmosa-usb-menu-runtime/lib/ld-linux-armhf.so.3 \
    --library-path /tmp/drmosa-usb-menu-runtime/lib \
    "$MOUNT/usr/bin/doveos-desktop" "$@"
