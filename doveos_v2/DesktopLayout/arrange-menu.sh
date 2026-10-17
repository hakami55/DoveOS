#!/bin/sh
set -eu
menu=${1:?JWM menu path required}
awk '
 /<Menu .*label="Programs"/ { section="programs" }
 /<Menu .*label="Media"/ { section="media" }
 section=="programs" && /<Program label="(HDMI In|Wireless DeX|DoveAir|AirPlay Preview|DoveDeX)"/ { next }
 section=="media" && /<Program label="(USB HDMI[^"]*|DovePlayer)"/ { next }
 /<\/Menu>/ {
  if(section=="programs") {
   print "  <Program label=\"DoveAir\" icon=\"/media/usb0/DoveOS-v2/AppIcons/dove-airplay-v2.png\">/tmp/doveos-theme/doveos-open doveos-airplay-ui /bin/sh /media/usb0/DoveOS-v2/AirPlay/launch.sh</Program>"
   print "  <Program label=\"DoveDeX\" icon=\"/media/usb0/DoveOS-v2/AppIcons/dove-dex-v2.png\">/tmp/doveos-theme/doveos-open dovedex /bin/sh /media/usb0/DoveOS-v2/dovedex/launch.sh</Program>"
  }
  if(section=="media") print "  <Program label=\"DovePlayer\" icon=\"/media/usb0/DoveOS-v2/DovePlayer/icon.png\">/bin/sh /media/usb0/DoveOS-v2/DovePlayer/launch.sh</Program>"
  section=""
 }
 { print }
' "$menu" > "$menu.arrange-next"
# Keep the inode of menus bound into the desktop runtime.
cat "$menu.arrange-next" > "$menu"
rm -f "$menu.arrange-next"
