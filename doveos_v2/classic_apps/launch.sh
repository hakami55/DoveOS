#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2/ClassicApps
awk '$2=="/media/usb0" {found=1} END {exit !found}' /proc/mounts || { echo 'DoveOS USB is not mounted' >&2;exit 2; }
case "${1:-}" in
 minesweeper|calculator|notepad) name=doveos-$1;shift;;
 *) echo 'Choose minesweeper, calculator or notepad' >&2; exit 2;;
esac
(cd "$B"; sha256sum -c files.sha256) >/tmp/doveos-classic-apps-check.log 2>&1
mkdir -p /tmp/doveos-classic-apps
expected=$(awk -v name="$name" '$2==name{print $1}' "$B/files.sha256")
cp "$B/$name" "/tmp/doveos-classic-apps/$name.new.$$"
test "$(sha256sum "/tmp/doveos-classic-apps/$name.new.$$" | cut -d ' ' -f1)" = "$expected"
chmod 700 "/tmp/doveos-classic-apps/$name.new.$$"
mv -f "/tmp/doveos-classic-apps/$name.new.$$" "/tmp/doveos-classic-apps/$name"
export DISPLAY="${DISPLAY:-:7}" HOME="${HOME:-/root}"
export GTK_PATH="${GTK_PATH:-/tmp/doveos-theme/gtk-modules}"
export GDK_PIXBUF_MODULE_FILE="${GDK_PIXBUF_MODULE_FILE:-/tmp/doveos-v2-pixbuf.cache}"
TZ=${TZ:-AST-3}
if [ -r /media/usb0/DoveOS-v2/home/time-zone.txt ];then TZ=$(cat /media/usb0/DoveOS-v2/home/time-zone.txt);fi
export TZ
exec "/tmp/doveos-classic-apps/$name" "$@"
