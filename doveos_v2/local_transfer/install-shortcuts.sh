#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2
T=/tmp/doveos-theme
M=/tmp/doveos-v2-root
cp "$B/Transfer/Transfer.desktop" "$T/live-etc/Transfer.desktop"
chmod 755 "$T/live-etc/Transfer.desktop"
mkdir -p "$B/home/.local/share/applications"
cp "$B/Transfer/Transfer.desktop" "$B/home/.local/share/applications/Dove-transfer.desktop"
for file in "$T/jwmrc" "$T/live-etc/jwmrc";do
 test -f "$file" || continue
 if ! grep -q 'Transfer/launch.sh' "$file";then
  sed -i '/<Program label="Files"/a\    <Program label="Local Transfer" icon="/media/usb0/DoveOS-v2/Transfer/transfer.png">/bin/sh /media/usb0/DoveOS-v2/Transfer/launch.sh</Program>' "$file"
 fi
done
if [ "${1:-}" = --live ];then
 cp "$B/Transfer/Transfer.desktop" "$M/root/Desktop/Transfer.desktop"
 chmod 755 "$M/root/Desktop/Transfer.desktop"
 cp "$T/live-etc/jwmrc" "$M/root/.jwmrc"
 chroot "$M" /usr/bin/env DISPLAY=:7 /usr/bin/jwm -reload
fi
