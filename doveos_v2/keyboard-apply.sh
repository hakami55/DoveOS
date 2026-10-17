#!/bin/sh
# Executed inside the desktop root. Fixed arguments only, no firmware writes.
set -eu
export DISPLAY=:7
F=/media/usb0/DoveOS-v2/home/keyboard-settings.txt
layout=0;toggle=0;delay=500;interval=40
if [ -f "$F" ];then
 IFS=' ' read -r tag swap delay interval layout toggle < "$F"
 [ "$tag" = DOVE_KEYBOARD1 ] || exit 1
fi
case "$delay:$interval" in *[!0-9:]*|:*) exit 1;;esac
[ "$delay" -ge 200 ] && [ "$delay" -le 2000 ] && [ "$interval" -ge 20 ] && [ "$interval" -le 250 ] || exit 1
L=/media/usb0/DoveOS-v2/home/keyboard-layouts.txt
C=/tmp/doveos-theme/keyboard-layouts.tsv
if [ -f "$L" ];then
 exec 4< "$L"
 IFS= read -r version <&4
 [ "$version" = DOVE_LAYOUTS1 ] || exit 1
 count=0;layouts=;previous=
 while IFS= read -r id <&4;do
  case "$id" in ''|*[!a-z0-9_]*)exit 1;;esac
  [ "$count" -lt 2 ] && [ "$id" != "$previous" ] || exit 1
  awk -F '\t' -v id="$id" '$1==id {found=1} END {exit !found}' "$C"
  test -f "/usr/share/X11/xkb/symbols/$id"
  if [ "$count" -eq 0 ];then layouts=$id;else layouts=$layouts,$id;fi
  previous=$id;count=$((count+1))
 done
 exec 4<&-
 [ "$count" -ge 1 ] || exit 1
else
 case "$layout" in 0) layouts=us;;1) layouts=ara;;2) layouts=us,ara;;*)exit 1;;esac
fi
case "$toggle" in 0) option=grp:alt_shift_toggle;;1) option=grp:win_space_toggle;;*)exit 1;;esac
setxkbmap -model pc105 -layout "$layouts" -option '' -option "$option"
/usr/bin/doveos-status --select-layout 0
xset r rate "$delay" "$((1000 / interval))"
printf 'Keyboard layout and repeat settings applied.\n' > /tmp/doveos-keyboard-status
