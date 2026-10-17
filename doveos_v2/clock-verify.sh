set -e
M=/tmp/doveos-v2-root
ROOT=/media/usb0/DoveOS-v2
TZ=AST-3 date
sha256sum "$M/usr/bin/doveos-settings-ui" "$M/usr/bin/doveos-status"
cat "$ROOT/desktop.log"
cat "$ROOT/session.log" | tail -12
for directory in /proc/[0-9]*; do
 [ "$(readlink "$directory/root" 2>/dev/null)" = "$M" ] || continue
 exe=$(readlink "$directory/exe" 2>/dev/null || :)
 case "$exe" in *jwm*|*doveos-status*) echo "RUNNING $exe";; esac
done
chroot "$M" /bin/sh -c 'DISPLAY=:7 HOME=/root GTK_PATH=/tmp/doveos-theme/gtk-modules LD_LIBRARY_PATH=/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf /usr/bin/doveos-settings-ui datetime > /tmp/doveos-date-settings.log 2>&1 </dev/null &' 
sleep 2
cat /tmp/doveos-date-settings.log
