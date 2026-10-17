#!/bin/sh
# HP autorun hook; skips safe-removal state and returns without blocking boot.
test ! -e /tmp/doveos-shutting-down || exit 0
test -r /media/usb0/DoveOS-v2/EthernetRTL8153/start.sh || exit 0
/bin/sh /media/usb0/DoveOS-v2/EthernetRTL8153/start.sh start
