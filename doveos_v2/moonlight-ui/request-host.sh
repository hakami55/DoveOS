#!/bin/sh
set -eu
R=/tmp/drmosa-usb-menu-runtime
Q=/tmp/doveos-moonlight-ui
action=${1:-}
host=${2:-}
case "$action" in
 discover) exec "$R/lib/ld-linux-armhf.so.3" --library-path "$R/lib" "$R/moonlight-discover";;
 check|pair|stop|unpair) case "$host" in ''|*[!0-9.]*) echo 'Invalid PC address';exit 2;;esac;;
 *) exit 2;;
esac
if [ "$action" = pair ];then
 exec "$R/lib/ld-linux-armhf.so.3" --library-path "$R/lib" "$R/moonlight-pair" "$host" "$R/keys"
fi
exec "$R/lib/ld-linux-armhf.so.3" --library-path "$R/lib" "$Q/host-control" "$action" "$host" "$R/keys"
