M=/tmp/doveos-v2-root
cat /tmp/doveos-date-settings.log
ps w | grep '[d]oveos-settings'
chroot "$M" /bin/sh -c 'DISPLAY=:7 HOME=/root GTK_PATH=/tmp/doveos-theme/gtk-modules GDK_PIXBUF_MODULE_FILE=/tmp/doveos-v2-pixbuf.cache LD_LIBRARY_PATH=/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf nohup /usr/bin/doveos-settings-ui datetime > /tmp/doveos-date-settings.log 2>&1 </dev/null &'
sleep 2
head -8 /tmp/doveos-date-settings.log
ps w | grep '[d]oveos-settings'
