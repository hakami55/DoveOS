M=/tmp/doveos-v2-root
for d in /proc/[0-9]*;do
 r=$(readlink "$d/root" 2>/dev/null)
 case "$r" in "$M"*) echo "CHROOT_PID ${d##*/} $r";kill "${d##*/}" 2>/dev/null;;esac
done
sleep 2
for d in /proc/[0-9]*;do
 [ "$(readlink "$d/root" 2>/dev/null)" != "$M" ] || kill -9 "${d##*/}" 2>/dev/null
done
sleep 1
for path in root media/usb0 media tmp sys proc dev/pts dev;do umount "$M/$path" 2>/dev/null || :;done
umount "$M" || exit 1
rm -f /tmp/doveos-v2.pid
/bin/sh /media/usb0/DoveOS-v2/start-desktop.sh start
