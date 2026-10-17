#!/bin/sh
# Apply the requested dove shortcut and Start icons, retaining launch commands.
set -eu
B=/media/usb0/DoveOS-v2
A=$B/AppIcons
T=/tmp/doveos-theme
(cd "$A";sha256sum -c icons.sha256) >/tmp/doveos-app-icons-check.log
if [ -d "$T/live-etc" ];then
 cp "$A/dove-start-v2.png" "$T/dove-start.png"
 cp "$A/dove-start-v2.png" "$T/live-etc/dove-start.png"
fi
for pair in AirPlay:dove-airplay-v2 DoveDeX:dove-dex-v2;do
 name=${pair%:*};icon=$A/${pair#*:}.png
 for folder in "$T/live-etc" "$T/light-desktops" "$B/home/Desktop" "/tmp/doveos-v2-root/root/Desktop" "$B/home/.local/share/applications" "$B/AirPlay" "$B/dovedex";do
  file=$folder/$name.desktop
  [ -f "$file" ] || continue
  sed "s|^Icon=.*|Icon=$icon|" "$file" > "$file.icon-next"
  cmp -s "$file" "$file.icon-next" && { rm -f "$file.icon-next";continue; }
  cat "$file.icon-next" > "$file";rm -f "$file.icon-next"
 done
done
if [ -f "$T/live-etc/jwmrc" ];then
 sed -e '/<Program label="AirPlay Preview"/s|icon="[^"]*"|icon="/media/usb0/DoveOS-v2/AppIcons/dove-airplay-v2.png"|' \
     -e '/<Program label="DoveDeX"/s|icon="[^"]*"|icon="/media/usb0/DoveOS-v2/AppIcons/dove-dex-v2.png"|' \
     -e '/<TrayButton .*label="Start"/s|icon="[^"]*"|icon="/media/usb0/DoveOS-v2/AppIcons/dove-start-v2.png"|' \
     "$T/live-etc/jwmrc" > "$T/live-etc/jwmrc.icon-next"
 cat "$T/live-etc/jwmrc.icon-next" > "$T/live-etc/jwmrc";rm -f "$T/live-etc/jwmrc.icon-next"
fi
