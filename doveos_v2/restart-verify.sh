set -e
M=/tmp/doveos-v2-root
n=0
while ! pidof jwm >/dev/null 2>&1 && [ "$n" -lt 15 ];do n=$((n+1));sleep 1;done
pidof jwm >/dev/null
before=$(pidof jwm)
echo "BEFORE_RESTART=$before"
chroot "$M" /bin/sh -c 'DISPLAY=:7 HOME=/root LD_LIBRARY_PATH=/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf /usr/bin/jwm -p'
chroot "$M" /bin/sh -c 'DISPLAY=:7 HOME=/root LD_LIBRARY_PATH=/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf /usr/bin/jwm -restart'
sleep 5
echo "AFTER_RESTART=$(pidof jwm)"
cat /media/usb0/DoveOS-v2/desktop.log
cat /tmp/doveos-v2-jwm.log
ps w | grep -E 'drmosa-usb-menu /|jwm|doveos-v2-start' | grep -v grep
