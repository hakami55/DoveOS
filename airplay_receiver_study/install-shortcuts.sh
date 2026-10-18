#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2
T=/tmp/doveos-theme
test -r "$B/AirPlay/AirPlay.desktop"
cp "$B/AirPlay/AirPlay.desktop" "$T/live-etc/"
mkdir -p "$B/home/.local/share/applications"
cp "$B/AirPlay/AirPlay.desktop" "$B/home/.local/share/applications/"
if ! grep -q 'label="DoveAir"' "$T/live-etc/jwmrc";then
 sed '/<Program label="DoveDeX"/a\  <Program label="DoveAir" icon="/media/usb0/DoveOS-v2/AppIcons/dove-airplay-v2.png">/tmp/doveos-theme/doveos-open doveos-airplay-ui /bin/sh /media/usb0/DoveOS-v2/AirPlay/launch.sh</Program>' "$T/live-etc/jwmrc" > "$T/live-etc/jwmrc.airplay-new"
 mv "$T/live-etc/jwmrc.airplay-new" "$T/live-etc/jwmrc"
fi
