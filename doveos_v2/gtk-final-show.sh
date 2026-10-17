M=/tmp/doveos-v2-root
n=0
while [ "$n" -lt 25 ];do
 ready=0
 for pid in $(pidof jwm);do [ "$(readlink /proc/$pid/root)" = "$M" ] && ready=1;done
 [ "$ready" = 0 ] || break
 n=$((n+1));sleep 1
done
[ "$ready" = 1 ] || { tail -8 /media/usb0/DoveOS-v2/desktop.log;exit 1; }
chroot "$M" /bin/sh -c 'export DISPLAY=:7 HOME=/root GTK_PATH=/tmp/doveos-theme/gtk-modules LD_LIBRARY_PATH=/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf XDG_RUNTIME_DIR=/tmp/doveos-v2-runtime XDG_CONFIG_HOME=/root/.config GDK_PIXBUF_MODULE_FILE=/tmp/doveos-v2-pixbuf.cache; doveos-settings-ui >/tmp/gtk-settings.log 2>&1 &'
sleep 2
cat /tmp/gtk-settings.log /tmp/gtk-sdk.log
for pid in $(pidof doveos-settings-ui doveos-sdk-demo);do echo PROCESS=$pid;grep -e VmRSS -e VmSize /proc/$pid/status;done
ls /media/usb0/DoveOS-v2/SDK
echo GTK_APPS_CHECKED
