#!/bin/sh
set -eu
source_dir=${1:-/etc/doveos}
desktop_dir=${2:-/root/Desktop}
mkdir -p "$desktop_dir"
for shortcut in "$desktop_dir"/*.desktop;do
 [ -f "$shortcut" ] || continue
 rm -f "$shortcut"
done
for name in AirPlay DoveDeX Moonlight Files Terminal Transfer DovePlayer;do
 [ -r "$source_dir/$name.desktop" ] || { echo "Missing desktop shortcut: $name" >&2;exit 1; }
 cp "$source_dir/$name.desktop" "$desktop_dir/$name.desktop"
 chmod 755 "$desktop_dir/$name.desktop"
done

# Optional HDMI receiver.
if [ -r "$source_dir/HDMI-In.desktop" ];then
 cp "$source_dir/HDMI-In.desktop" "$desktop_dir/HDMI-In.desktop"
 chmod 755 "$desktop_dir/HDMI-In.desktop"
fi
