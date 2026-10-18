#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2/Capture/Test
(cd "$B";sha256sum -c test-helpers.sha256) > /tmp/doveos-hdmi-test-install.log
for dir in /tmp/doveos-theme/live-etc /media/usb0/DoveOS-v2/home/.local/share/applications /media/usb0/DoveOS-v2/home/Desktop /tmp/doveos-v2-root/root/Desktop;do
 [ -d "$dir" ] || continue
 cp "$B/HDMI-In.desktop" "$dir/HDMI-In.desktop"
 chmod 755 "$dir/HDMI-In.desktop"
 rm -f "$dir/HDMI-Test.desktop"
done
for menu in /tmp/doveos-theme/live-etc/jwmrc /tmp/doveos-theme/jwmrc /tmp/doveos-v2-root/root/.jwmrc;do
 [ -f "$menu" ] || continue
 # Preserve the inode: menus may be bound into the desktop runtime.
 awk '
 /<Program label="(USB HDMI Capture \(Preview\)|HDMI Test|HDMI In|HDMI in)"/ {next}
 {print}
 /<Menu .*label="Media"/ {
  print "  <Program label=\"HDMI In\" icon=\"video-display.png\">/tmp/doveos-theme/doveos-open doveos-hdmi-test /bin/sh /media/usb0/DoveOS-v2/Capture/Test/test-launch.sh</Program>"
 }
 ' "$menu" > "$menu.hdmi-next"
 cat "$menu.hdmi-next" > "$menu"
 rm -f "$menu.hdmi-next"
done
echo HDMI_IN_SHORTCUT_INSTALLED
