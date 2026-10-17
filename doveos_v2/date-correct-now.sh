set -e
M=/tmp/doveos-v2-root
ROOT=/media/usb0/DoveOS-v2
cp "$ROOT/clock-set" /tmp/doveos-clock-set
chmod 700 /tmp/doveos-clock-set
"$M/lib/ld-linux-armhf.so.3" --library-path "$M/lib/arm-linux-gnueabihf:$M/usr/lib/arm-linux-gnueabihf" /tmp/doveos-clock-set 1790889914
printf '1790889914\n' > "$ROOT/home/time-last-set.txt"
