ROOT=/media/usb0/DoveOS-v2
cat "$ROOT/desktop.log"
cat "$ROOT/session.log" | tail -15
cat /tmp/doveos-v2.pid
mount | tail -20
ls -l /tmp/doveos-v2-root /tmp/doveos-theme
