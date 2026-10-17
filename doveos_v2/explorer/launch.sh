#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2/Explorer
mkdir -p /tmp/doveos-explorer-runtime
(cd "$B"; sha256sum -c files.sha256) >/tmp/doveos-explorer-check.log 2>&1
copy_checked(){
 name=$1;target=$2
 expected=$(awk -v name="$name" '$2==name {print $1}' "$B/files.sha256")
 test "${#expected}" -eq 64
 cp "$B/$name" "$target.new.$$"
 signature=$(sha256sum "$target.new.$$")
 test "${signature%% *}" = "$expected" || { rm -f "$target.new.$$"; exit 3; }
 chmod 700 "$target.new.$$"
 mv -f "$target.new.$$" "$target"
}
copy_checked doveos-explorer /tmp/doveos-explorer-runtime/explorer
copy_checked libz.so.1 /tmp/doveos-explorer-runtime/libz.so.1
export DISPLAY="${DISPLAY:-:7}" HOME="${HOME:-/root}"
export GTK_PATH="${GTK_PATH:-/tmp/doveos-theme/gtk-modules}"
export GDK_PIXBUF_MODULE_FILE="${GDK_PIXBUF_MODULE_FILE:-/tmp/doveos-v2-pixbuf.cache}"
export LD_LIBRARY_PATH="/tmp/doveos-explorer-runtime:${LD_LIBRARY_PATH:-/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf}"
exec /tmp/doveos-explorer-runtime/explorer "$@"
