#!/bin/sh
# Runs on the stock host; the OMX decoder never enters the optional chroot.
M=/tmp/doveos-v2-root
exec "$M/lib/ld-linux-armhf.so.3" --library-path "$M/usr/lib/arm-linux-gnueabihf:$M/lib/arm-linux-gnueabihf:/usr/lib:/lib" /tmp/doveos-vlc-rtk-host
