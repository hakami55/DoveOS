#!/bin/sh
export HOME=/root DISPLAY=:7
export GTK_PATH=/tmp/doveos-theme/gtk-modules
export GDK_PIXBUF_MODULE_FILE=/tmp/doveos-v2-pixbuf.cache
export LD_LIBRARY_PATH=/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf
remote=/media/usb0/DoveOS-v2/RemoteLink
if [ -r "$remote/tailscale-exec.ext2" ] && [ -r "$remote/remote-link.sh" ];then
 chroot /proc/1/root /bin/sh "$remote/remote-link.sh" start > /tmp/doveos-remote-launch.log 2>&1 || :
 selected=$(sed -n 's/^selected=//p' /media/usb0/DoveOS-v2/home/moonlight-ui.ini | head -n 1)
 if [ -n "$selected" ] && [ -r "$remote/tailnet-hosts" ] && grep -Fxq "$selected" "$remote/tailnet-hosts";then
  chroot /proc/1/root /bin/sh "$remote/remote-link.sh" ready "$selected" >> /tmp/doveos-remote-launch.log 2>&1 || :
 fi
fi
/tmp/doveos-moonlight-ui/doveos-moonlight-ui "$@"
code=$?
if [ "$code" -eq 10 ];then
 printf 'moonlight\n' > /tmp/doveos-v2-action
 jwm -exit
fi
exit "$code"
