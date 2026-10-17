#!/bin/sh
# Start DoveOS from removable media using the existing Lap Dock USB handler.
set -eu
umask 077
U=/media/usb0
B=$U/DoveOS-v2
R=/tmp/drmosa-usb-menu-runtime
# The stock handler temporarily renames autorun while running its RAM copy.
if [ ! -e "$U/autorun" ] && [ -d "$U/autorun.old" ];then mv "$U/autorun.old" "$U/autorun";fi
test -f "$B/AUTO_START" || exit 0
test -f "$B/start-desktop.sh"
expected=cb3b26154ce5f07113c614a2d7f03c59ac113333340f96966c31f9e7cdb295c2
actual=$(sha256sum /usr/local/bin/IMS_Modules/wfd_dfb | cut -d ' ' -f 1)
test "$actual" = "$expected" || { echo DOVEOS_FIRMWARE_BUILD_UNSUPPORTED;exit 4; }
mkdir -p "$R/lib" "$R/keys" "$B/home" "$U/DrMoSa/WiFi" "$U/DrMoSa/keys"
if ! /bin/sh "$B/Memory/assets-runtime.sh" legacy;then
 cp "$U"/DrMoSa/lib/* "$R/lib/"
 for pair in dock-stream-hw:dock-stream-hw drmosa-moonlight-stop:moonlight-stop drmosa-hdmi-viewer:hdmi-viewer drmosa-media-player:media-player drmosa-media-fb-mirror:media-fb-mirror doveos-control:doveos-control dock-moonlight-pair:moonlight-pair dock-check-pair:moonlight-check dock-discover-pc:moonlight-discover;do
  src=${pair%:*};dst=${pair#*:};cp "$U/DrMoSa/$src" "$R/$dst";chmod 700 "$R/$dst"
 done
fi
for name in settings.sh wifi.sh wifi-dhcp.sh;do
 cp "$U/DrMoSa/$name" "$R/$name";chmod 700 "$R/$name"
done
for file in "$U"/DrMoSa/keys/*;do
 test -f "$file" || continue
 cp "$file" "$R/keys/";chmod 600 "$R/keys/${file##*/}"
done
chmod 700 "$R/lib/ld-linux-armhf.so.3"
# New users configure their own network in Control Panel. No network is seeded.
if [ -f "$U/DrMoSa/WiFi/network.conf" ];then
 (/bin/sh "$R/wifi.sh" boot; /bin/sh "$R/wifi.sh" monitor) > "$U/DrMoSa/WiFi/startup.log" 2>&1 </dev/null &
fi
# Standard kernel-supported Ethernet adapters use the existing host network.
# The development-only direct-PC Ethernet bridge is excluded from this image.
if [ -r "$B/Controllers/service.sh" ];then
 /bin/sh "$B/Controllers/service.sh" start >/dev/null 2>&1 || :
fi
exec /bin/sh "$B/start-desktop.sh" start
