#!/bin/sh
# Runs within the DoveOS desktop. The playback helper alone enters the host root.
set -eu
B=/media/usb0/DoveOS-v2/DovePlayer
Q=/tmp/doveplayer
mkdir -p "$Q"
(cd "$B";sha256sum -c binaries.sha256) >/dev/null
cp "$B/doveplayer-ui" "$Q/doveplayer-ui"
chmod 755 "$Q/doveplayer-ui"
export HOME=/root DISPLAY=:7
export GTK_PATH=/tmp/doveos-theme/gtk-modules
export GDK_PIXBUF_MODULE_FILE=/tmp/doveos-v2-pixbuf.cache
export LD_LIBRARY_PATH=/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf
exec "$Q/doveplayer-ui" "$@"
