#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2/dovedex
K=/media/usb0/DoveOS-v2/backups/dovedex-wifi1080-20261006
test ! -d /tmp/dovedex-runtime-v1/session-lock || { echo 'Close the stream first.';exit 2; }
for d in /proc/[0-9]*;do
 [ -r "$d/cmdline" ] || continue
 cmd=$(tr '\000' ' ' < "$d/cmdline")
 case "$cmd" in '/tmp/doveos-dovedex'|'/tmp/doveos-dovedex '*) echo 'Close DoveDeX first.';exit 2;;esac
done
test "$(sha256sum "$K/runtime.tar.gz" | cut -d' ' -f1)" = 1fdb74a25ce9a83cd6cc137492294954db6be20990f4c7bb8b84f4a00c14486f
test "$(sha256sum "$K/backend.sh" | cut -d' ' -f1)" = 20ec97858f81c91a5d859e614a6f50645cb451f82c3c2ef612a65a7da43c8340
(cd "$K";sha256sum -c backup.sha256)
for f in runtime.tar.gz backend.sh README.txt dovedex-direct-host dovedex-direct-host.c hevc_backend.h final.sha256 package-current.sha256;do
 [ ! -f "$K/$f" ] || cp "$K/$f" "$B/$f"
done
cp "$K/profiles.ini" /tmp/doveos-v2-root/root/.config/dovedex/profiles.ini
sync
echo 'Previous DoveDeX runtime and profiles restored. Reopen DoveDeX normally.'
