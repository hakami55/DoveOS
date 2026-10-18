#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2
P=$B/YouTubeTrial
T=/tmp/doveos-theme
M=/tmp/doveos-v2-root
test -r "$P/launch.sh" && test -r "$P/youtube-ui"
mkdir -p "$B/home/.local/share/applications"
cat > "$B/home/.local/share/applications/DoveCast.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=DoveCast
Comment=YouTube casting receiver preview
Icon=/media/usb0/DoveOS-v2/AppIcons/dove-cast-48.png
Exec=/tmp/doveos-theme/doveos-open DoveCast /bin/sh /media/usb0/DoveOS-v2/YouTubeTrial/launch.sh
Terminal=false
Categories=Network;AudioVideo;
EOF
if test -d "$T/live-etc";then cp "$B/home/.local/share/applications/DoveCast.desktop" "$T/live-etc/";fi
for desktop in "$B/home/Desktop" "$M/root/Desktop";do
 test -d "$desktop" || continue
 cp "$B/home/.local/share/applications/DoveCast.desktop" "$desktop/DoveCast.desktop"
 chmod 755 "$desktop/DoveCast.desktop"
done
for menu in "$T/live-etc/jwmrc" "$T/jwmrc" "$M/root/.jwmrc";do
 test -f "$menu" || continue
 awk '
 /<Program label="DoveCast"/ {next}
 {print}
 /<Menu .*label="Programs"/ {
  print "  <Program label=\"DoveCast\" icon=\"/media/usb0/DoveOS-v2/AppIcons/dove-cast-24.png\" tooltip=\"YouTube casting receiver preview\">/tmp/doveos-theme/doveos-open DoveCast /bin/sh /media/usb0/DoveOS-v2/YouTubeTrial/launch.sh</Program>"
  added=1
 }
 END {if(!added)exit 2}
 ' "$menu" > "$menu.dovecast-next" || { rm -f "$menu.dovecast-next";exit 2; }
 cat "$menu.dovecast-next" > "$menu"
 rm -f "$menu.dovecast-next"
done
echo DOVECAST_START_MENU_INSTALLED
