#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2
T=/tmp/doveos-theme
Q=/tmp/doveos-moonlight-ui
mkdir -p "$Q"
(cd "$B/Moonlight";sha256sum -c runtime.sha256)
gunzip -c "$B/Moonlight/runtime.tar.gz" | tar -xf - -C "$Q"
(cd "$Q";sha256sum -c files.sha256)
chmod 700 "$Q/doveos-moonlight-ui" "$Q/host-control" "$Q/launch.sh"
sed -i 's|/usr/bin/doveos-moonlight|/tmp/doveos-moonlight-ui/launch.sh|g' "$T/live-etc/jwmrc" "$T/live-etc/Moonlight.desktop"
mkdir -p "$B/home/.local/share/applications"
cp "$T/live-etc/Moonlight.desktop" "$B/home/.local/share/applications/Moonlight.desktop"
echo MOONLIGHT_SDK_UI_INSTALLED
