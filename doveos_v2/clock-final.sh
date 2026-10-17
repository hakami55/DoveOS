set -e
M=/tmp/doveos-v2-root
for directory in /proc/[0-9]*;do
 [ "$(readlink "$directory/root" 2>/dev/null)" = "$M" ] || continue
 exe=$(readlink "$directory/exe" 2>/dev/null || :)
 case "$exe" in *doveos-settings-ui*) kill "${directory##*/}" 2>/dev/null || :;; esac
done
sleep 1
mkdir -p /tmp/doveos-clock-update
gunzip -c /media/usb0/DoveOS-v2/classic-theme.tar.gz | tar -x -C /tmp/doveos-clock-update
umount "$M/usr/bin/doveos-settings-ui"
mount --bind /tmp/doveos-clock-update/doveos-settings-ui "$M/usr/bin/doveos-settings-ui"
chroot "$M" /bin/sh -c 'DISPLAY=:7 HOME=/root GTK_PATH=/tmp/doveos-theme/gtk-modules GDK_PIXBUF_MODULEDIR=/usr/lib/arm-linux-gnueabihf/gdk-pixbuf-2.0/2.10.0/loaders GDK_PIXBUF_MODULE_FILE=/tmp/doveos-v2-pixbuf.cache LD_LIBRARY_PATH=/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf /usr/bin/doveos-settings-ui datetime > /tmp/doveos-date-settings.log 2>&1 </dev/null &'
sleep 2
head -5 /tmp/doveos-date-settings.log
TZ=AST-3 date
sha256sum "$M/usr/bin/doveos-settings-ui"
echo DATE_TIME_OPENED
