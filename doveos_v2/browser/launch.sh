#!/bin/sh
set -eu
if [ ! -r /usr/local/bin/IMS_Modules/wfd_dfb ];then
 test "$(sha256sum /proc/1/root/usr/local/bin/IMS_Modules/wfd_dfb | cut -d ' ' -f1)" = cb3b26154ce5f07113c614a2d7f03c59ac113333340f96966c31f9e7cdb295c2
 exec /usr/sbin/chroot /proc/1/root /bin/sh /media/usb0/DoveOS-v2/ModernBrowser/launch.sh "$@"
fi
B=/media/usb0/DoveOS-v2
P=$B/ModernBrowser
M=/tmp/doveos-v2-root
T=/tmp/modern-browser-tools
Q=/tmp/browser-webkit-work
lock=/tmp/dovebrowser-running
if ! mkdir "$lock" 2>/dev/null;then
 if test -f "$lock/pid" && kill -0 "$(cat "$lock/pid")" 2>/dev/null;then exit 0;fi
 rm -f "$lock/pid";rmdir "$lock";mkdir "$lock"
fi
echo $$ > "$lock/pid"
old=$(cat /proc/sys/vm/swappiness);min=$(cat /proc/sys/vm/min_free_kbytes)
cleanup(){
 trap - EXIT INT TERM
 if test -f "$Q/engine.pid";then
  pid=$(cat "$Q/engine.pid")
  if test -r /proc/$pid/cmdline && tr '\000' ' ' < /proc/$pid/cmdline | grep -q '/tmp/modern-browser-tools/browser-run';then kill -TERM "$pid" 2>/dev/null || :;sleep 2;fi
 fi
 echo "$old" > /proc/sys/vm/swappiness;echo "$min" > /proc/sys/vm/min_free_kbytes
 /bin/sh "$P/unmount.sh" || echo BROWSER_RUNTIME_STILL_BUSY
 /bin/sh "$P/reserve.sh" stop || echo BROWSER_RESERVE_COULD_NOT_STOP
 rm -f "$lock/pid";rmdir "$lock"
}
trap cleanup EXIT INT TERM
awk '$2=="/media/usb0"&&$4~/(^|,)rw(,|$)/{ok=1}END{exit !ok}' /proc/mounts
(cd "$P";sha256sum -c files.sha256) > /tmp/dovebrowser-check.log
mkdir -p "$T"
for name in browser-run direct-image dovebrowser swap-on;do
 if ! cmp -s "$P/$name" "$T/$name";then cp "$P/$name" "$T/$name.new";chmod 700 "$T/$name.new";mv "$T/$name.new" "$T/$name";fi
done
if ! cmp -s "$P/images.sha256" "$T/images.verified";then
 (cd "$P";sha256sum -c images.sha256) > /tmp/dovebrowser-images-check.log
 cp "$P/images.sha256" "$T/images.verified"
fi
/bin/sh "$P/reserve.sh" start
/bin/sh "$P/mount.sh"
echo 100 > /proc/sys/vm/swappiness;echo 8192 > /proc/sys/vm/min_free_kbytes
chroot "$M" /usr/bin/env DISPLAY=:7 HOME=/root GTK_PATH=/tmp/doveos-theme/gtk-modules GDK_PIXBUF_MODULE_FILE=/tmp/doveos-v2-pixbuf.cache "$T/dovebrowser" "$@"
