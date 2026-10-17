#!/bin/sh
# Run from the dock host shell after exiting DoveDeX. Restores the prior app only.
set -eu
B=/media/usb0/DoveOS-v2
OLD=$B/backups/dovedex-direct-20261003
test -f "$OLD/dovedex-backup.complete"
test -f "$OLD/x11-bridge"
test ! -d /tmp/dovedex-runtime-v1/session-lock
# Do not replace mapped executables until their owned app/server processes exit.
for f in /proc/[0-9]*/cmdline;do
 [ -r "$f" ] || continue
 cmd=$(tr '\000' ' ' < "$f" 2>/dev/null || :)
 case "$cmd" in '/tmp/doveos-dovedex '*|'/tmp/dovedex-runtime-v1/dovedex-run '*)
  pid=${f#/proc/};pid=${pid%/cmdline};kill "$pid" 2>/dev/null || :;;esac
done
sleep 3
cp -a "$OLD/dovedex/." "$B/dovedex/"
cp "$OLD/x11-bridge" "$B/x11-bridge"
if [ -f "$OLD/profiles.ini" ];then cp "$OLD/profiles.ini" /tmp/doveos-v2-root/root/.config/dovedex/profiles.ini;fi
rm -f /tmp/dovedex-runtime-v1/.verified /tmp/dovedex-runtime-v1/server-usb.pid /tmp/dovedex-runtime-v1/server-wifi.pid
sync
printf 'restart\n' > /tmp/doveos-v2-action
touch /tmp/doveos-v2-stop
echo 'Prior DoveDeX app and bridge restored; DoveOS desktop restarting.'
