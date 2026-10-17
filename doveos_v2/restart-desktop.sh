touch /tmp/doveos-v2-stop
n=0
while [ -f /tmp/doveos-v2.pid ] && [ "$n" -lt 20 ];do sleep 1;n=$((n+1));done
[ ! -f /tmp/doveos-v2.pid ] || exit 1
/bin/sh /media/usb0/DoveOS-v2/start-desktop.sh start
