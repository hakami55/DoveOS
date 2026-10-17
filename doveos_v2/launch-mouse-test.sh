chroot /tmp/doveos-v2-root /bin/sh -c '
export DISPLAY=:7 HOME=/root GTK_PATH=/tmp/doveos-theme/gtk-modules GDK_PIXBUF_MODULE_FILE=/tmp/doveos-v2-pixbuf.cache LD_LIBRARY_PATH=/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf
/tmp/doveos-theme/doveos-mouse-test >/tmp/doveos-mouse-test.log 2>&1 &
p=$!
sleep 2
kill -0 "$p" && echo MOUSE_TEST_RUNNING
cat /tmp/doveos-mouse-test.log
'
