#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2/dovedex
EXPECTED=bb3a4fbeb04f5e5d9037b6671a1ac0dae78545399df10eb8495fc7f31baa73b0
hash=$(sha256sum "$B/dovedex")
test "${hash%% *}" = "$EXPECTED" || { echo DOVEDEX_USB_HASH_MISMATCH >&2;exit 2; }
RAM=/tmp/doveos-dovedex
cp "$B/dovedex" "$RAM.new.$$"
hash=$(sha256sum "$RAM.new.$$")
test "${hash%% *}" = "$EXPECTED" || { rm -f "$RAM.new.$$";exit 3; }
chmod 755 "$RAM.new.$$";mv "$RAM.new.$$" "$RAM"
export DISPLAY="${DISPLAY:-:7}" HOME="${HOME:-/root}"
export GTK_PATH="${GTK_PATH:-/tmp/doveos-theme/gtk-modules}"
export GDK_PIXBUF_MODULE_FILE="${GDK_PIXBUF_MODULE_FILE:-/tmp/doveos-v2-pixbuf.cache}"
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf}"
RUNHASH=7c0940751d0465fa4613ccf02e8bb3e8fdf86c8c8bb0165b8d61047dd0d13e93
hash=$(sha256sum "$B/dovedex-run");test "${hash%% *}" = "$RUNHASH"
cp "$B/dovedex-run" /tmp/dovedex-ui-run.new
chmod 700 /tmp/dovedex-ui-run.new;mv /tmp/dovedex-ui-run.new /tmp/dovedex-ui-run
exec /tmp/dovedex-ui-run --exec "$RAM" "$@"
