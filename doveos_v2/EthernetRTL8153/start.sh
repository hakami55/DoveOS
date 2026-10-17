#!/bin/sh
set -eu
PATH=/usr/bin:/bin:/usr/sbin:/sbin;export PATH
P=/media/usb0/DoveOS-v2/EthernetRTL8153
R=/tmp/doveos-rtl8153
case ${1:-start} in
 stop) test ! -d "$R" || touch "$R/stop";exit 0;;
 start) ;;
 *)exit 2;;
esac
test ! -e /media/usb0/DRMOSA_DISABLE_ETHERNET || exit 0
if test -r "$R/service.pid";then
 p=$(cat "$R/service.pid")
 case "$p" in ''|*[!0-9]*)exit 1;;esac
 if kill -0 "$p" 2>/dev/null && tr '\000' ' ' < /proc/$p/cmdline | grep -qF '/tmp/doveos-rtl8153/service.sh';then exit 0;fi
fi
mkdir -p "$R"
mkdir "$R/launch.lock" 2>/dev/null || exit 0
trap 'rmdir "$R/launch.lock" 2>/dev/null || :' EXIT
(cd "$P" && sha256sum -c SHA256SUMS >/dev/null)
cp "$P/ecm-bridge" "$R/ecm-bridge.next"
cp "$P/service.sh" "$R/service.sh.next"
chmod 700 "$R/ecm-bridge.next"
mv "$R/ecm-bridge.next" "$R/ecm-bridge"
mv "$R/service.sh.next" "$R/service.sh"
rm -f "$R/stop"
nohup /bin/sh "$R/service.sh" > "$R/service.log" 2>&1 < /dev/null &
sleep 1
echo RTL8153_AUTOMATIC_SERVICE_STARTED
