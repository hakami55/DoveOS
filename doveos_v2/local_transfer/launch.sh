#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2/Transfer
R=/tmp/doveos-transfer-runtime
mkdir -p "$R"
(cd "$B"; sha256sum -c files.sha256) > /tmp/doveos-transfer-check.log 2>&1
for name in doveos-transfer libz.so.1;do
 expected=$(awk -v name="$name" '$2==name{print $1}' "$B/files.sha256")
 test "${#expected}" -eq 64
 cp "$B/$name" "$R/$name.new.$$"
 actual=$(sha256sum "$R/$name.new.$$")
 test "${actual%% *}" = "$expected" || exit 3
 chmod 700 "$R/$name.new.$$"
 mv "$R/$name.new.$$" "$R/$name"
done
export DISPLAY="${DISPLAY:-:7}" HOME="${HOME:-/root}"
export GTK_PATH="${GTK_PATH:-/tmp/doveos-theme/gtk-modules}"
export GDK_PIXBUF_MODULE_FILE="${GDK_PIXBUF_MODULE_FILE:-/tmp/doveos-v2-pixbuf.cache}"
export LD_LIBRARY_PATH="$R:${LD_LIBRARY_PATH:-/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf}"
exec "$R/doveos-transfer" "$@"
