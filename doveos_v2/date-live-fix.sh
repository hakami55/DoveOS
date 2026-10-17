M=/tmp/doveos-v2-root
for d in /proc/[0-9]*;do
 exe=$(readlink "$d/exe" 2>/dev/null || :)
 case "$exe" in *doveos-settings-ui*) echo "$d $exe ROOT=$(readlink "$d/root")";kill -9 "${d##*/}";; esac
done
sleep 1
umount "$M/usr/bin/doveos-settings-ui"
mount --bind /tmp/doveos-clock-update/doveos-settings-ui "$M/usr/bin/doveos-settings-ui"
chroot "$M" /bin/sh -c 'DISPLAY=:7 HOME=/root GTK_PATH=/tmp/doveos-theme/gtk-modules GDK_PIXBUF_MODULE_FILE=/tmp/doveos-v2-pixbuf.cache LD_LIBRARY_PATH=/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf /usr/bin/doveos-settings-ui datetime > /tmp/doveos-date-settings.log 2>&1 </dev/null &'
sleep 1
sha256sum "$M/usr/bin/doveos-settings-ui"
head -3 /tmp/doveos-date-settings.log
TZ=AST-3 date
