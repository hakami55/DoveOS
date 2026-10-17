#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2/controller-tester
(cd "$B";sha256sum -c files.sha256) >/tmp/doveos-controller-tester-check.log 2>&1 || exit 2
mkdir -p /tmp/doveos-xbox-tester-lib
copy_checked(){
 name=$1;dst=$2
 expected=$(awk -v file="$name" '$2==file{print $1}' "$B/files.sha256")
 test "${#expected}" -eq 64
 cp "$B/$name" "$dst.new.$$"
 signature=$(sha256sum "$dst.new.$$")
 if [ "${signature%% *}" != "$expected" ];then rm -f "$dst.new.$$";exit 3;fi
 chmod 700 "$dst.new.$$"
 mv -f "$dst.new.$$" "$dst"
}
copy_checked libz.so.1 /tmp/doveos-xbox-tester-lib/libz.so.1
copy_checked doveos-controller-tester /tmp/doveos-controller-tester
export DISPLAY="${DISPLAY:-:7}" HOME="${HOME:-/root}"
export GTK_PATH="${GTK_PATH:-/tmp/doveos-theme/gtk-modules}"
export GDK_PIXBUF_MODULE_FILE="${GDK_PIXBUF_MODULE_FILE:-/tmp/doveos-v2-pixbuf.cache}"
export LD_LIBRARY_PATH="/tmp/doveos-xbox-tester-lib:${LD_LIBRARY_PATH:-/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf}"
exec /tmp/doveos-controller-tester
