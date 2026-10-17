set -e
M=/tmp/doveos-v2-root
ROOT=/media/usb0/DoveOS-v2
mkdir -p "$ROOT/home"
printf 'AST-3\n' > "$ROOT/home/time-zone.txt"
cp "$ROOT/clock-set" /tmp/doveos-clock-set
chmod 700 /tmp/doveos-clock-set
"$M/lib/ld-linux-armhf.so.3" --library-path "$M/lib/arm-linux-gnueabihf:$M/usr/lib/arm-linux-gnueabihf" /tmp/doveos-clock-set 1790887590
TZ=AST-3 date
M=/tmp/doveos-v2-root
touch /tmp/doveos-v2-stop
# Ending only this desktop's window manager releases its X server grab.
for pid in $(pidof jwm); do
    [ "$(readlink /proc/$pid/root)" = "$M" ] || continue
    kill "$pid"
done
n=0
while [ -f /tmp/doveos-v2.pid ] && [ "$n" -lt 15 ]; do
    n=$((n+1)); sleep 1
done
[ ! -f /tmp/doveos-v2.pid ] || { echo DESKTOP_STOP_TIMEOUT; exit 1; }
/bin/sh /media/usb0/DoveOS-v2/start-desktop.sh start
echo RESIZE_LOCK_RECOVERY_STARTED
