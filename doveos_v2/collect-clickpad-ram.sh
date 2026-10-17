set -e
kill "$(cat /tmp/doveos-clickpad-ram.pid)" 2>/dev/null || :
n=0
while ! grep -q KEYBOARD_INTERFACE_RESTORED /tmp/doveos-clickpad-ram.log && [ "$n" -lt 5 ]; do
 n=$((n+1)); sleep 1
done
grep KEYBOARD_INTERFACE_RESTORED /tmp/doveos-clickpad-ram.log
cp /media/usb0/DoveOS-v2/backup-send /tmp/doveos-backup-send
chmod 700 /tmp/doveos-backup-send
/tmp/doveos-v2-root/lib/ld-linux-armhf.so.3 --library-path /tmp/doveos-v2-root/usr/lib/arm-linux-gnueabihf:/tmp/doveos-v2-root/lib/arm-linux-gnueabihf /tmp/doveos-backup-send /tmp/doveos-clickpad-ram.log 169.254.244.136 18817
