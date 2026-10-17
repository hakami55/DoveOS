#!/bin/sh
# Runs in the stock host namespace. All post-desktop work executes from RAM.
set -eu
cd /tmp
B=/media/usb0/DoveOS-v2
P=$B/Power
R=/tmp/doveos-power-action
verb=${1:-}
action=${2:-}
case "$action" in standby|restart|cpu|gpu|unmount) ;; *) echo 'This power action is unavailable.';exit 2;;esac
case "$verb" in --check|--request|--worker) ;; *) exit 2;;esac
fail(){ echo "$*";exit 3; }
system_device(){
 device=$(awk '$2=="/media/usb0"{print $1}' /proc/mounts)
 case "$device" in /dev/sd[a-z][1-9]) ;; *) fail 'System USB is not mounted normally.';;esac
 test -r "$B/start-desktop.sh" && test -r "$B/doveos-v2.ext2" || fail 'DoveOS system files are unavailable.'
 uuid=$(blkid "$device" | sed -n 's/.* UUID="\([^"]*\)".*/\1/p')
 test -n "$uuid" || fail 'Could not identify the system USB.'
}
check(){
 test ! -e /tmp/doveos-shutting-down || fail 'A power or storage operation is already in progress.'
 test ! -e /tmp/doveos-direct-display && test ! -e /tmp/doveos-usb-video-active || fail 'Close the streaming or video session first.'
 test ! -d /tmp/doveplayer/playing || fail 'Stop video playback first.'
 system_device
 test -d "$P" && (cd "$P";sha256sum -c files.sha256) >/dev/null 2>&1 || fail 'Power tools failed their integrity check.'
 if test "$action" = standby;then
  test -e "$B/home/power-screen-verified" || fail 'Screen wake has not been verified.'
  # Snapshot first: the producer can publish a newer timestamp while we read.
  snapshot=$(cat /tmp/doveos-backlight-state 2>/dev/null) || fail 'Screen control is not ready.'
  read up idle < /proc/uptime
  printf '%s\n' "$snapshot" | awk -v up="$up" '$1=="DOVE_BACKLIGHT1"&&$3>=3&&$3<=100&&up-$2>=-0.02&&up-$2<3{ok=1}END{exit !ok}' || fail 'Screen control is not ready.'
  found=0
  for proc in /proc/[0-9]*;do
   test -r "$proc/cmdline" || continue
   tr '\000' ' ' < "$proc/cmdline" 2>/dev/null | grep -q ' /tmp/doveos-power-manager ' && found=1 || :
  done
  test "$found" = 1 || fail 'The screen power service is not running.'
 fi
 if test "$action" = gpu;then test -r "$B/GPU/desktop/gpu-desktop-host.sh" || fail 'GPU desktop files are unavailable.';fi
}
if test "$verb" != --worker;then
 check
 if test "$verb" = --check;then echo POWER_ACTION_READY;exit 0;fi
 mkdir /tmp/doveos-power-action.lock 2>/dev/null || fail 'Another power action is already running.'
 trap 'rmdir /tmp/doveos-power-action.lock 2>/dev/null || :' EXIT
 mkdir -p "$R"
 for file in host.sh unmount-system.sh power-card files.sha256;do cp "$P/$file" "$R/$file";done
 (cd "$R";sha256sum -c files.sha256) >/dev/null
 chmod 700 "$R/power-card"
 cat /proc/sys/kernel/random/boot_id > "$R/boot"
 printf '%s\n' "$device" > "$R/device"
 printf '%s\n' "$uuid" > "$R/uuid"
 rm -f "$R/result" /tmp/doveos-system-usb-unmounted
 nohup /bin/sh "$R/host.sh" --worker "$action" > "$R/action.log" 2>&1 </dev/null &
 trap - EXIT
 echo POWER_ACTION_ACCEPTED
 exit 0
fi
trap 'rmdir /tmp/doveos-power-action.lock 2>/dev/null || :' EXIT
test "$(cat "$R/boot")" = "$(cat /proc/sys/kernel/random/boot_id)" || fail 'Boot changed.'
(cd "$R";sha256sum -c files.sha256) >/dev/null
sleep 2
check
test "$device" = "$(cat "$R/device")" && test "$uuid" = "$(cat "$R/uuid")" || fail 'System USB changed; action cancelled.'
case "$action" in
 standby)
  touch /tmp/doveos-power-standby
  echo STANDBY_REQUESTED > "$R/result"
  exit 0;;
 cpu|gpu)
  printf '%s\n' "$action" > "$B/home/desktop-renderer.txt.power-next"
  mv "$B/home/desktop-renderer.txt.power-next" "$B/home/desktop-renderer.txt"
  if test "$action" = gpu;then rm -f /tmp/doveos-gpu-disabled;fi
  sync
  printf 'restart\n' > /tmp/doveos-v2-action
  touch /tmp/doveos-v2-stop
  echo "RENDERER_RESTART_REQUESTED $action" > "$R/result"
  exit 0;;
esac
if ! sh "$R/unmount-system.sh" "$device" "$uuid";then
 echo UNMOUNT_FAILED_DO_NOT_REMOVE > "$R/result"
 "$R/power-card" failed || :
 exit 4
fi
if test "$action" = restart;then
 echo RESTARTING > "$R/result"
 "$R/power-card" restarting || :
 sync
 /sbin/reboot
 exit 0
fi
printf '%s\n' "$uuid" > /tmp/doveos-system-usb-unmounted
echo SYSTEM_USB_SAFE_TO_REMOVE > "$R/result"
"$R/power-card" safe || { echo SAFE_BUT_DISPLAY_UNAVAILABLE >> "$R/result";exit 5; }
