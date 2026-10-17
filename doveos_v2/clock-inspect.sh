echo CLOCK_CAPABILITY
/bin/busybox --list | grep -e '^ntpd$' -e '^nc$' -e '^date$'
ls /sys/class/rtc 2>/dev/null
cat /media/usb0/DoveOS-v2/home/time-zone.txt 2>/dev/null
/bin/date -u
