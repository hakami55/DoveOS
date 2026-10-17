#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2
P=$B/ModernBrowser
T=/tmp/doveos-theme
M=/tmp/doveos-v2-root
(cd "$P";sha256sum -c files.sha256) >/tmp/dovebrowser-install-check.log
mkdir -p "$B/home/.local/share/applications"
cp "$P/DoveBrowser.desktop" "$B/home/.local/share/applications/"
if test -d "$T/live-etc";then cp "$P/DoveBrowser.desktop" "$T/live-etc/";fi
for menu in "$T/live-etc/jwmrc" "$T/jwmrc" "$M/root/.jwmrc";do
 test -f "$menu" || continue
 awk '
 /<Program label="(DoveBrowser \(Preview\)|Browser \(Modern\))"/ {next}
 {print}
 /<Program label="Browser \(NetSurf\)"/ {
  print "  <Program label=\"DoveBrowser (Preview)\" icon=\"/media/usb0/DoveOS-v2/ClassicApps/icons/browser-24.png\">/tmp/doveos-theme/doveos-open DoveBrowser /bin/sh /media/usb0/DoveOS-v2/ModernBrowser/launch.sh</Program>"
  added=1
 }
 END {if(!added)exit 2}
 ' "$menu" > "$menu.browser-next"
 cat "$menu.browser-next" > "$menu";rm -f "$menu.browser-next"
done
echo DOVEBROWSER_INSTALLED
