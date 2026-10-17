#!/bin/sh
# Called by apply-theme on boot; --live also refreshes the current Start menu.
set -eu
B=/media/usb0/DoveOS-v2
T=/tmp/doveos-theme
M=/tmp/doveos-v2-root
E=$B/Explorer
test -r "$E/Files.desktop"
mkdir -p "$B/DrMoSa/Media" "$B/DrMoSa/Documents" "$B/DrMoSa/Downloads"
cp "$E/Files.desktop" "$T/live-etc/Files.desktop"
chmod 755 "$T/live-etc/Files.desktop"
mkdir -p "$B/home/.local/share/applications"
cp "$E/Files.desktop" "$B/home/.local/share/applications/Dove-explorer.desktop"
# Only file-launch commands change. The desktop's pcmanfm process stays running.
for file in "$T/jwmrc" "$T/live-etc/jwmrc" "$T/keyboard-action.sh";do
 test -f "$file" || continue
 sed -i \
  -e 's|/tmp/doveos-theme/doveos-open pcmanfm pcmanfm /media/usb0/DrMoSa/Media|/bin/sh /media/usb0/DoveOS-v2/Explorer/launch.sh /media/usb0/DrMoSa/Media|g' \
  -e 's|/tmp/doveos-theme/doveos-open pcmanfm pcmanfm /media/usb0|/bin/sh /media/usb0/DoveOS-v2/Explorer/launch.sh /media/usb0/DrMoSa/Documents|g' "$file"
done
if [ "${1:-}" = --live ];then
 cp "$E/Files.desktop" "$M/root/Desktop/Files.desktop"
 chmod 755 "$M/root/Desktop/Files.desktop"
 cp "$T/live-etc/jwmrc" "$M/root/.jwmrc"
 chroot "$M" /usr/bin/env DISPLAY=:7 /usr/bin/jwm -reload
fi
