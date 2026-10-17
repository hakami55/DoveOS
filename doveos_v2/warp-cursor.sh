cp /media/usb0/DoveOS-v2/cursor-probe /tmp/doveos-v2-cursor-probe
chmod 700 /tmp/doveos-v2-cursor-probe
/tmp/doveos-v2-root/lib/ld-linux-armhf.so.3 --library-path /tmp/doveos-v2-root/usr/lib/arm-linux-gnueabihf:/tmp/doveos-v2-root/lib/arm-linux-gnueabihf /tmp/doveos-v2-cursor-probe 100 100
