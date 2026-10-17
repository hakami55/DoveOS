M=/tmp/doveos-v2-root
chroot "$M" /bin/sh -c '
 export DISPLAY=:7 HOME=/root GTK_PATH=/tmp/doveos-theme/gtk-modules GDK_PIXBUF_MODULE_FILE=/tmp/doveos-v2-pixbuf.cache LD_LIBRARY_PATH=/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf
 /usr/bin/doveos-settings-ui datetime >/tmp/doveos-date-settings.log 2>&1 &
 p=$!
 sleep 2
 if kill -0 "$p" 2>/dev/null;then echo APP_ALIVE=$p;else wait "$p";echo APP_EXIT=$?;fi
 '
cat /tmp/doveos-date-settings.log
