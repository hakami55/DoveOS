#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2
APP=${1:-};shift || exit 2
case "$APP" in
 browser) PROGRAM=/usr/bin/netsurf-gtk;CLASS=netsurf-gtk;;
 word) PROGRAM=/usr/bin/abiword;CLASS=Abiword;;
 pdf) PROGRAM=/usr/lib/mupdf/mupdf-x11;CLASS=MuPDF
  if [ "$#" = 0 ];then
   file=$(/tmp/doveos-theme/light-file-picker) || exit 0
   [ -n "$file" ] || exit 0
   set -- "$file"
  fi;;
 *) exit 2;;
esac
chroot /proc/1/root /bin/sh "$B/light-mount.sh" >/tmp/doveos-light-mount.log 2>&1
L=/proc/1/root/tmp/doveos-light-root
/tmp/doveos-light-chroot "$L" "$PROGRAM" "$@" >>"/tmp/doveos-$APP.log" 2>&1 &
app=$!
if [ -x /tmp/doveos-theme/light-launch-busy ];then /tmp/doveos-theme/light-launch-busy "$app" "$CLASS" >>"/tmp/doveos-$APP.log" 2>&1;fi
wait "$app"
