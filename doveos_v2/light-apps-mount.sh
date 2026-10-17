#!/bin/sh
set -e
B=/media/usb0/DoveOS-v2
M=/tmp/doveos-light-root
test -f "$B/light-apps.ext2" || exit 2
if ! awk -v target="$M" '$2 == target {found=1} END {exit !found}' /proc/mounts;then
 mkdir -p "$M"
 mount -t ext2 -o loop,ro "$B/light-apps.ext2" "$M"
 mount --bind /dev "$M/dev"
 mount --bind /proc "$M/proc"
 mount --bind /sys "$M/sys"
 mount --bind /tmp "$M/tmp"
 mount --bind /media/usb0 "$M/media/usb0"
 mount --bind "$B/home" "$M/root"
fi
mkdir -p "$B/home/Documents" "$B/home/.config/gtk-3.0" /tmp/doveos-light-cache
if [ ! -f "$B/home/.config/gtk-3.0/settings.ini" ];then
 printf '[Settings]\ngtk-theme-name=Chicago95\ngtk-icon-theme-name=DoveClassic\ngtk-font-name=MS Sans Serif 8\n' > "$B/home/.config/gtk-3.0/settings.ini"
fi
if [ ! -f /tmp/doveos-light-pixbuf.cache ];then
 chroot "$M" /bin/sh -c 'unset GTK_PATH; export LD_LIBRARY_PATH=/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf; /usr/lib/arm-linux-gnueabihf/gdk-pixbuf-2.0/gdk-pixbuf-query-loaders' > /tmp/doveos-light-pixbuf.cache
fi
echo LIGHT_APPS_MOUNTED
