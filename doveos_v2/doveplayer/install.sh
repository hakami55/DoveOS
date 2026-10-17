#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2
P=$B/DovePlayer
mkdir -p "$B/home/.local/share/applications"
cp "$P/DovePlayer.desktop" "$B/home/.local/share/applications/DovePlayer.desktop"
if [ -d /tmp/doveos-theme/live-etc ];then cp "$P/DovePlayer.desktop" /tmp/doveos-theme/live-etc/DovePlayer.desktop;fi
if [ -d /tmp/doveos-v2-desktop ];then cp "$P/DovePlayer.desktop" /tmp/doveos-v2-desktop/DovePlayer.desktop;chmod 755 /tmp/doveos-v2-desktop/DovePlayer.desktop;fi
