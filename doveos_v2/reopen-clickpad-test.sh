set -e
echo TRIAL_READER
tail -3 /tmp/doveos-clickpad-trial.log
echo MOUSE_TEST_PATHS
ls -l /tmp/doveos-v2-root/tmp/doveos-theme/doveos-mouse-test /tmp/doveos-v2-root/usr/bin/doveos-mouse-test /media/usb0/DoveOS-v2/theme/doveos-mouse-test /tmp/doveos-theme/doveos-mouse-test 2>/dev/null || :
echo MOUSE_TEST_ERROR
cat /tmp/doveos-v2-root/tmp/doveos-mouse-test.log 2>/dev/null || :
nohup chroot /tmp/doveos-v2-root /bin/sh -c '
 export DISPLAY=:7 HOME=/root GTK_PATH=/tmp/doveos-theme/gtk-modules GDK_PIXBUF_MODULE_FILE=/tmp/doveos-v2-pixbuf.cache LD_LIBRARY_PATH=/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf
 exec /tmp/doveos-theme/doveos-mouse-test
' >/tmp/doveos-mouse-test-reopened.log 2>&1 </dev/null &
p=$!
sleep 2
kill -0 "$p" && echo MOUSE_TEST_REOPENED
cat /tmp/doveos-mouse-test-reopened.log
