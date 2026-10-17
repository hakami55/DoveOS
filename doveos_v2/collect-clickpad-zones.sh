kill "$(cat /tmp/doveos-clickpad-zones.pid)" 2>/dev/null || :
sed -n '/^report /p' /tmp/doveos-clickpad-zones.log > /tmp/doveos-clickpad-report-lines.txt
cp /media/usb0/DoveOS-v2/backup-send /tmp/doveos-backup-send
chmod 700 /tmp/doveos-backup-send
/tmp/doveos-v2-root/lib/ld-linux-armhf.so.3 --library-path /tmp/doveos-v2-root/usr/lib/arm-linux-gnueabihf:/tmp/doveos-v2-root/lib/arm-linux-gnueabihf /tmp/doveos-backup-send /tmp/doveos-clickpad-report-lines.txt 169.254.244.136 18817
