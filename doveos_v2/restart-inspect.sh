ROOT=/media/usb0/DoveOS-v2
for file in desktop.log session.log bridge.log;do echo "$file";tail -18 "$ROOT/$file";done
echo JWM_LOG
tail -25 /tmp/doveos-v2-jwm.log
ps w | grep -E 'jwm|doveos-v2|drmosa-usb-menu' | grep -v grep
sha256sum "$ROOT/classic-theme.tar.gz" "$ROOT/start-desktop.sh"
echo AUTORUN
head -65 /media/usb0/autorun/01-drmosa-menu.sh
