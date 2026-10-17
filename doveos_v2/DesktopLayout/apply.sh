#!/bin/sh
set -eu
theme=/tmp/doveos-theme
source_dir=$theme/live-etc
session=$theme/doveos-session
menu=$source_dir/jwmrc
test -f "$source_dir/AirPlay.desktop"
test -f "$session"
test -f "$menu"

# AirPlay keeps its receiver and icon; only its visible name changes.
sed -i 's/^Name=AirPlay Preview$/Name=DoveAir/' "$source_dir/AirPlay.desktop"
for shortcut in /media/usb0/DoveOS-v2/AirPlay/AirPlay.desktop \
                /media/usb0/DoveOS-v2/home/.local/share/applications/AirPlay.desktop;do
 [ -f "$shortcut" ] || continue
 if grep -q '^Name=AirPlay Preview$' "$shortcut";then
  sed -i 's/^Name=AirPlay Preview$/Name=DoveAir/' "$shortcut"
 fi
done
sed -i 's/label="AirPlay Preview"/label="DoveAir"/' "$menu"

# The session receives a fresh RAM desktop each time DoveOS starts.
if grep -q '^cp /etc/doveos/\*\.desktop /root/Desktop/$' "$session";then
 sed 's|^cp /etc/doveos/\*\.desktop /root/Desktop/$|/bin/sh /media/usb0/DoveOS-v2/DesktopLayout/desktop-filter.sh /etc/doveos /root/Desktop|' "$session" > "$session.layout-new"
 # doveos-session is already file-bound into the runtime. Keep its inode.
 cat "$session.layout-new" > "$session"
 rm -f "$session.layout-new"
fi
grep -q '^/bin/sh /media/usb0/DoveOS-v2/DesktopLayout/desktop-filter.sh /etc/doveos /root/Desktop$' "$session"

# Existing Start categories contain Minesweeper, Notes and SDK Demo,
# Control Panel, Media and VLC. Add the two tools that lacked menu entries.
if ! grep -q '<Program label="Controller Tester"' "$menu";then
 awk '
  /<Program label="(Games|Minesweeper)"/ && !added {
   print "  <Program label=\"Controller Tester\" icon=\"/media/usb0/DoveOS-v2/ClassicApps/icons/controller-24.png\">/tmp/doveos-theme/doveos-open doveos-controller-tester /bin/sh /media/usb0/DoveOS-v2/controller-tester/launch.sh</Program>"
   added=1
  }
  { print }
  END { if (!added) exit 2 }
 ' "$menu" > "$menu.layout-new"
 mv "$menu.layout-new" "$menu"
fi
/bin/sh /media/usb0/DoveOS-v2/DesktopLayout/arrange-menu.sh "$menu"

# The live desktop's menu reads the same JWM configuration after reload.
cp "$menu" "$theme/jwmrc"
