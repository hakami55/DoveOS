#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2
L=/tmp/doveos-light-root
mounted=$(awk -v target="$L" '$2==target{print $1}' /proc/mounts)
needs_mount=1
if [ -n "$mounted" ];then
 expected=$(awk '{print $1}' "$B/loop-image-check.sha256")
 test "$(sha256sum "$B/loop-image-check" | awk '{print $1}')" = "$expected" || exit 4
 cp "$B/loop-image-check" /tmp/doveos-light-loop-check.new
 chmod 700 /tmp/doveos-light-loop-check.new
 mv /tmp/doveos-light-loop-check.new /tmp/doveos-light-loop-check
 if /tmp/doveos-light-loop-check "$mounted" "$B/light-apps.ext2";then
  current_usb=$(awk '$2=="/media/usb0"{print $1}' /proc/mounts)
  bound_usb=$(awk -v target="$L/media/usb0" '$2==target{print $1}' /proc/mounts)
  [ "$current_usb" != "$bound_usb" ] || needs_mount=0
 else
  result=$?;test "$result" -eq 1 || exit 4
 fi
fi
if [ "$needs_mount" -eq 1 ];then
 for entry in /proc/[0-9]*;do
  if [ "$(readlink "$entry/root" 2>/dev/null || :)" = "$L" ];then echo LIGHT_APPS_STALE_ROOT_BUSY;exit 4;fi
 done
 if [ -n "$mounted" ];then
  for child in usr/share/mime etc root media/usb0 tmp sys proc dev/shm dev;do umount "$L/$child" 2>/dev/null || :;done
 fi
 /bin/sh "$B/root-mount-guard.sh" "$B" "$L" "$B/light-apps.ext2"
fi
test -r "$L/usr/bin/netsurf-gtk"
bind(){
 if ! awk -v target="$2" '$2==target {found=1} END {exit !found}' /proc/mounts;then mount --bind "$1" "$2";fi
}
bind /dev "$L/dev"
bind /dev/shm "$L/dev/shm"
bind /proc "$L/proc"
bind /sys "$L/sys"
bind /tmp "$L/tmp"
bind /media/usb0 "$L/media/usb0"
mkdir -p "$B/light-apps-home/.config/gtk-3.0" "$B/light-apps-home/.local/share/fonts" /media/usb0/DrMoSa/Documents
for f in dove-sans.ttf dove-sans-bold.ttf;do
 if [ -r "$B/home/.local/share/fonts/$f" ] && [ ! -r "$B/light-apps-home/.local/share/fonts/$f" ];then cp "$B/home/.local/share/fonts/$f" "$B/light-apps-home/.local/share/fonts/";fi
done
if [ ! -r "$B/light-apps-home/.config/gtk-3.0/settings.ini" ];then
 printf '[Settings]\ngtk-theme-name=Chicago95\ngtk-font-name=Microsoft Sans Serif 8\ngtk-enable-animations=false\n' >"$B/light-apps-home/.config/gtk-3.0/settings.ini"
fi
printf 'XDG_DOCUMENTS_DIR="/media/usb0/DrMoSa/Documents"\nXDG_DOWNLOAD_DIR="/media/usb0/DrMoSa/Downloads"\n' >"$B/light-apps-home/.config/user-dirs.dirs"
mkdir -p /media/usb0/DrMoSa/Downloads
bind "$B/light-apps-home" "$L/root"
E=/tmp/doveos-light-etc
if ! awk -v target="$L/etc" '$2==target {found=1} END {exit !found}' /proc/mounts;then
 mkdir -p "$E";cp -a "$L/etc/." "$E/";mount --bind "$E" "$L/etc"
fi
if [ -r /etc/resolv.conf ];then cp /etc/resolv.conf "$E/resolv.conf";fi
# Use the current validated Wi-Fi lease when the firmware only supplied time-service routes.
if [ -r /tmp/doveos-wifi/router ];then
 gateway=$(cat /tmp/doveos-wifi/router)
 case "$gateway" in ''|*[!0-9.]*) gateway=;;esac
 if [ -n "$gateway" ] && /bin/busybox ip route show | grep -q 'dev wlan0';then
  if ! /bin/busybox ip route show | grep -q '^default ';then /bin/busybox ip route add default via "$gateway" dev wlan0 metric 100;fi
  if [ ! -s "$E/resolv.conf" ];then printf 'nameserver %s\n' "$gateway" > "$E/resolv.conf";fi
 fi
fi
mkdir -p "$E/fonts"
if [ ! -r "$E/fonts/fonts.conf" ];then
 cat >"$E/fonts/fonts.conf" <<'EOF'
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<fontconfig><dir>/usr/share/fonts</dir><dir>/root/.local/share/fonts</dir><cachedir>/root/.cache/fontconfig</cachedir><alias><family>sans-serif</family><prefer><family>DejaVu Sans</family></prefer></alias><alias><family>serif</family><prefer><family>DejaVu Serif</family></prefer></alias><alias><family>monospace</family><prefer><family>DejaVu Sans Mono</family></prefer></alias></fontconfig>
EOF
fi
if [ ! -r /tmp/doveos-light-loaders.cache ];then
 chroot "$L" /usr/lib/arm-linux-gnueabihf/gdk-pixbuf-2.0/gdk-pixbuf-query-loaders > /tmp/doveos-light-loaders.cache
fi
# Static MIME definitions can be paged out from the read-only asset image.
mime_source=
if [ -r "$B/Memory/assets-runtime.sh" ];then
 mime_source=$(/bin/sh "$B/Memory/assets-runtime.sh" mime 2>/dev/null) || mime_source=
fi
if [ -z "$mime_source" ] && [ -r "$B/mime-runtime.tar.gz" ];then
 if [ ! -r /tmp/doveos-light-share/mime/mime.cache ];then
  mkdir -p /tmp/doveos-light-share
  gunzip -c "$B/mime-runtime.tar.gz" | tar -xf - -C /tmp/doveos-light-share
 fi
 mime_source=/tmp/doveos-light-share/mime
fi
if [ -n "$mime_source" ] && [ -r "$mime_source/mime.cache" ];then
 current=$(awk -v point="$L/usr/share/mime" '$2==point{print $1}' /proc/mounts)
 if [ -n "$current" ] && [ ! "$mime_source/mime.cache" -ef "$L/usr/share/mime/mime.cache" ];then
  for entry in /proc/[0-9]*;do
   if [ "$(readlink "$entry/root" 2>/dev/null || :)" = "$L" ];then echo LIGHT_MIME_UPDATE_BUSY;exit 4;fi
  done
  umount "$L/usr/share/mime"
 fi
 bind "$mime_source" "$L/usr/share/mime"
fi
cp "$B/light-chroot" /tmp/doveos-light-chroot.new
chmod 755 /tmp/doveos-light-chroot.new
mv /tmp/doveos-light-chroot.new /tmp/doveos-light-chroot
echo LIGHT_APPS_READY
