#!/bin/sh
M=/tmp/doveos-v2-root
T=/tmp/doveos-theme
P=/tmp/doveos-timekeeper.pid
cp "$T/wifi-dhcp.sh" /tmp/drmosa-usb-menu-runtime/wifi-dhcp.sh
if [ -f "$P" ] && kill -0 "$(cat "$P")" 2>/dev/null;then exit 0;fi
cp "$T/doveos-timekeeper" /tmp/doveos-timekeeper
chmod 700 /tmp/doveos-timekeeper
/tmp/doveos-timekeeper --restore
(
 if [ -f /tmp/doveos-wifi/lease ] && [ ! -f /tmp/doveos-wifi/router ];then
  /bin/busybox udhcpc -B -n -q -t 2 -T 2 -i wlan0 -s /tmp/drmosa-usb-menu-runtime/wifi-dhcp.sh >/tmp/doveos-time-dhcp.log 2>&1 || :
 fi
 exec /tmp/doveos-timekeeper --daemon
) >/tmp/doveos-timekeeper.log 2>&1 </dev/null &
echo $! > "$P"
