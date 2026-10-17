#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2/ClassicApps
T=/tmp/doveos-theme
M=/tmp/doveos-v2-root
awk '$2=="/media/usb0" && $4 ~ /(^|,)rw(,|$)/ {found=1} END {exit !found}' /proc/mounts || { echo 'DoveOS USB is not mounted writable' >&2;exit 2; }
(cd "$B"; sha256sum -c files.sha256) >/tmp/doveos-classic-install-check.log 2>&1
mkdir -p "$T/live-etc" /media/usb0/DoveOS-v2/home/.local/share/applications
for dir in "$T/live-etc" "$T/light-desktops" "$M/root/Desktop" /media/usb0/DoveOS-v2/home/.local/share/applications /media/usb0/DoveOS-v2/home/Desktop;do
 [ -d "$dir" ] || continue
 rm -f "$dir/Mouse-Test.desktop" "$dir/Games.desktop" "$dir/Notes.desktop"
 for spec in SDK-Demo:sdk-48.png Controller-Tester:controller-48.png Moonlight:moonlight.png;do
  name=${spec%:*}; icon=${spec#*:}; file=$dir/$name.desktop
  [ -f "$file" ] || continue
  sed "s|^Icon=.*|Icon=$B/icons/$icon|" "$file" > /tmp/doveos-classic-shortcut.$$
  # Desktop entries can be bound individually: preserve their inode.
  cat /tmp/doveos-classic-shortcut.$$ > "$file"
 done
done
rm -f /tmp/doveos-classic-shortcut.$$
for name in Minesweeper Calculator Notepad;do
 cp "$B/$name.desktop" "$T/live-etc/"
 cp "$B/$name.desktop" /media/usb0/DoveOS-v2/home/.local/share/applications/
done
mkdir -p "$M/root/.local/share/applications"
cp "$B/Notepad.desktop" "$M/root/.local/share/applications/"
for mime in "$T/live-etc/mimeapps.list" "$M/root/.config/mimeapps.list" /media/usb0/DoveOS-v2/home/.config/mimeapps.list;do
 [ -f "$mime" ] || continue
 if grep -q '^text/plain=' "$mime";then
  sed 's|^text/plain=.*|text/plain=Notepad.desktop|' "$mime" > /tmp/doveos-notepad-mime.$$
 else
  sed '/^\[Default Applications\]/a text/plain=Notepad.desktop' "$mime" > /tmp/doveos-notepad-mime.$$
 fi
 cat /tmp/doveos-notepad-mime.$$ > "$mime"
done
rm -f /tmp/doveos-notepad-mime.$$
for menu in "$T/live-etc/jwmrc" "$T/jwmrc" "$M/root/.jwmrc";do
 [ -f "$menu" ] || continue
 sed \
  -e '/<Program label="Mouse Test"/d' \
  -e 's|<Program label="Notes"[^>]*>[^<]*</Program>|<Program label="Notepad" icon="/media/usb0/DoveOS-v2/ClassicApps/icons/notepad-24.png">/tmp/doveos-theme/doveos-open DoveNotepad /bin/sh /media/usb0/DoveOS-v2/ClassicApps/launch.sh notepad</Program>|' \
  -e 's|<Program label="Games"[^>]*>[^<]*</Program>|<Program label="Minesweeper" icon="/media/usb0/DoveOS-v2/ClassicApps/icons/mines-24.png">/tmp/doveos-theme/doveos-open DoveMinesweeper /bin/sh /media/usb0/DoveOS-v2/ClassicApps/launch.sh minesweeper</Program>|' \
  -e 's|<Program label="Calculator"[^>]*>[^<]*</Program>|<Program label="Calculator" icon="/media/usb0/DoveOS-v2/ClassicApps/icons/calculator-24.png">/tmp/doveos-theme/doveos-open DoveCalculator /bin/sh /media/usb0/DoveOS-v2/ClassicApps/launch.sh calculator</Program>|' \
  -e 's|label="SDK 2 Demo" icon="[^"]*"|label="SDK 2 Demo" icon="/media/usb0/DoveOS-v2/ClassicApps/icons/sdk-24.png"|' \
  -e 's|label="Controller Tester" icon="[^"]*"|label="Controller Tester" icon="/media/usb0/DoveOS-v2/ClassicApps/icons/controller-24.png"|' \
  -e 's|label="Moonlight" icon="[^"]*"|label="Moonlight" icon="/media/usb0/DoveOS-v2/ClassicApps/icons/moonlight.png"|' \
  -e 's|icon="light-browser.png"|icon="/media/usb0/DoveOS-v2/ClassicApps/icons/browser-24.png"|' \
  -e 's|icon="light-word.png"|icon="/media/usb0/DoveOS-v2/ClassicApps/icons/word-24.png"|' \
  -e 's|icon="light-pdf.png"|icon="/media/usb0/DoveOS-v2/ClassicApps/icons/pdf-24.png"|' \
  -e 's|label="HDMI In">|label="HDMI In" icon="/etc/doveos/classic-icons/24/video-display.png">|' \
  -e 's|label="Network">|label="Network" icon="/media/usb0/DoveOS-v2/ClassicApps/icons/network-24.png">|' \
  -e 's|label="Bluetooth">|label="Bluetooth" icon="/media/usb0/DoveOS-v2/ClassicApps/icons/bluetooth-24.png">|' \
  -e 's|label="Display">|label="Display" icon="/etc/doveos/classic-icons/24/preferences-desktop-display.png">|' \
  -e 's|label="Date \&amp; Time">|label="Date \&amp; Time" icon="/media/usb0/DoveOS-v2/ClassicApps/icons/time-24.png">|' \
  -e 's|label="Sound">|label="Sound" icon="/media/usb0/DoveOS-v2/ClassicApps/icons/sound-24.png">|' \
  "$menu" > /tmp/doveos-classic-menu.$$
 cat /tmp/doveos-classic-menu.$$ > "$menu"
done
rm -f /tmp/doveos-classic-menu.$$
echo DOVEOS_CLASSIC_APPS_INSTALLED
