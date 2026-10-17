#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2/dovedex
K=/media/usb0/DoveOS-v2/backups/dovedex-audio-20261006
R=/tmp/dovedex-runtime-v1
test ! -d "$R/session-lock" || { echo 'Close the stream before restoring.';exit 2; }
test -z "$(pidof doveos-dovedex || :)" || { echo 'Close the DoveDeX application before restoring.';exit 2; }
test "$(sha256sum "$K/app.tar.gz" | cut -d' ' -f1)" = 31d8b69b4c8636c981026ea58bfe315df383c54780e36b6624867d358d0276e3
for mode in usb wifi;do
 f="$R/server-$mode.pid"
 [ -r "$f" ] || continue
 p=$(cat "$f");case "$p" in ''|*[!0-9]*) exit 3;;esac
 if [ -r "/proc/$p/cmdline" ];then
  cmd=$(tr '\000' ' ' < "/proc/$p/cmdline")
  case "$cmd" in *"$R/dovedex-run 1800 $R/adb-$mode -P "*) kill -TERM "$p";;*) echo 'Server owner mismatch.';exit 3;;esac
 fi
 sleep 2;rm -f "$f"
done
gunzip -c "$K/app.tar.gz" | tar -xf - -C "$B"
profile=/tmp/doveos-v2-root/root/.config/dovedex/profiles.ini
if [ -f "$K/profiles.ini.before" ];then cp "$K/profiles.ini.before" "$profile";fi
sync
echo 'Previous DoveDeX package restored. Reopen DoveDeX normally.'
