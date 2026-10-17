#!/bin/sh
set -eu
B=${DOVEDEX_BASE:-/media/usb0/DoveOS-v2/dovedex}
R=/tmp/dovedex-runtime-v1
M=/tmp/doveos-v2-root
# Identity was rechecked over the authenticated agent before installing this.
test "$(sha256sum /usr/local/bin/IMS_Modules/wfd_dfb | cut -d' ' -f1)" = cb3b26154ce5f07113c614a2d7f03c59ac113333340f96966c31f9e7cdb295c2
# Identify the dock itself: USB hubs and debug Ethernet interfaces can change.
# Hardware compatibility is checked by the stock runtime checksum above.
rm -f /tmp/dovedex-rtk.sock /tmp/doveos-direct-display.ready
printf '%s\n' "$$" > /tmp/doveos-direct-display
ticks=0
while [ "$(cat /tmp/doveos-direct-display.ready 2>/dev/null || :)" != "$$" ] && [ "$ticks" -lt 10 ];do sleep 1;ticks=$((ticks+1));done
if [ "$(cat /tmp/doveos-direct-display.ready 2>/dev/null || :)" != "$$" ];then
 rm -f /tmp/doveos-direct-display /tmp/doveos-direct-display.ready
 echo 'Desktop bridge did not release the display. Restart DoveOS to load the updated bridge.';exit 5
fi
exec "$R/dovedex-run" 1800 "$M/lib/ld-linux-armhf.so.3" --library-path "$M/usr/lib/arm-linux-gnueabihf:$M/lib/arm-linux-gnueabihf:/usr/lib:/lib" "$R/dovedex-direct-host"
