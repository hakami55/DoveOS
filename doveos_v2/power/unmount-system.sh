#!/bin/sh
# Only the current DoveOS system volume. Never force or lazy-unmount a busy mount.
set -eu
cd /tmp
device=${1:-};uuid=${2:-}
case "$device" in /dev/sd[a-z][1-9]) ;; *) exit 2;;esac
test -n "$uuid"
test "$(awk '$2=="/media/usb0"{print $1}' /proc/mounts)" = "$device"
test "$(blkid "$device" | sed -n 's/.* UUID="\([^"]*\)".*/\1/p')" = "$uuid"
B=/media/usb0/DoveOS-v2
test -r "$B/start-desktop.sh"
test ! -e /tmp/doveos-direct-display
test ! -d /tmp/doveplayer/playing
touch /tmp/doveos-shutting-down
if test -x /tmp/doveos-timekeeper;then /tmp/doveos-timekeeper --save;fi
sh "$B/start-desktop.sh" stop
n=0
while test -e /tmp/doveos-v2.pid && test "$n" -lt 35;do sleep 1;n=$((n+1));done
test ! -e /tmp/doveos-v2.pid || { echo 'Desktop has not stopped. Do not remove USB.';exit 3; }
if [ -r "$B/RemoteLink/remote-link.sh" ];then
 sh "$B/RemoteLink/remote-link.sh" stop || {
  echo 'Remote Link remains active. Do not remove USB.';exit 12;
 }
fi
sh "$B/Memory/swap-runtime.sh" stop
# Stop the remaining DoveOS-owned USB users, including optional app runtimes.
for p in /proc/[0-9]*;do
 root=$(readlink "$p/root" 2>/dev/null || :)
 cmd=$(tr '\000' ' ' 2>/dev/null < "$p/cmdline" || :)
 case "$root" in /tmp/doveos-optional-root|/tmp/doveos-light-root) kill -TERM "${p##*/}" 2>/dev/null || :;;esac
 case "$cmd" in
  '/tmp/doveos-timekeeper --daemon '*|'/bin/sh /tmp/drmosa-usb-menu-runtime/wifi.sh monitor '*)
   for child in /proc/[0-9]*;do
    if test "$(cat "$child/comm" 2>/dev/null)" = sleep && test "$(awk '{print $4}' "$child/stat" 2>/dev/null)" = "${p##*/}";then kill -TERM "${child##*/}" 2>/dev/null || :;fi
   done
   kill -TERM "${p##*/}" 2>/dev/null || :;;
 esac
 exe=$(readlink "$p/exe" 2>/dev/null || :)
 case "$exe" in /tmp/doveos-v2-root/lib/arm-linux-gnueabihf/ld-2.31.so)
  case "$cmd" in *' /tmp/doveos-power-manager '*) kill -TERM "${p##*/}" 2>/dev/null || :;;esac;;
 esac
done
sleep 2
for path in $(awk '$2 ~ /^\/tmp\/doveos-(v2|optional|light)-root(\/|$)/ {p[++n]=$2} END {for(i=n;i>0;i--)print p[i]}' /proc/mounts);do
 umount "$path" || { echo "Still busy: $path. Do not remove USB.";exit 4; }
done
for path in /tmp/doveos-gpu-assets /tmp/doveos-memory-assets /tmp/doveos-mpv-trial /tmp/doveplayer-runtime /tmp/doveos-mplayer-trial /tmp/doveos-youtube-probe /tmp/doveos-airplay-runtime;do
 if awk -v p="$path" '$2==p{found=1}END{exit !found}' /proc/mounts;then umount "$path" || exit 5;fi
done
for loop in $(losetup -a | awk '/\/media\/usb0\/DoveOS-v2\// {sub(/:$/,"",$1);print $1}');do losetup -d "$loop" || exit 6;done
! grep -q '^/media/usb0/' /proc/swaps || exit 7
# Refuse any other bind mount of the system volume; do not unmount unknown users.
awk -v d="$device" '$1==d&&$2!="/media/usb0"{bad=1}END{exit bad}' /proc/mounts || exit 8
usb_busy_owners(){
 # Keep a RAM diagnostic while the worker is still alive: inherited handles
 # can disappear as soon as it exits. Never force an unknown user off USB.
 echo USB_BUSY_OWNERS_BEGIN
 for proc in /proc/[0-9]*;do
  for handle in "$proc"/cwd "$proc"/root "$proc"/exe "$proc"/fd/*;do
   target=$(readlink "$handle" 2>/dev/null || :)
   case "$target" in /media/usb0|/media/usb0/*)
    echo "$handle -> $target";cat "$proc/comm" 2>/dev/null || :;;
   esac
  done
  grep '/media/usb0/' "$proc/maps" 2>/dev/null || :
 done
 echo USB_BUSY_OWNERS_END
}
sync
attempt=0
while ! umount /media/usb0;do
 attempt=$((attempt+1))
 echo "USB_BUSY_RETRY $attempt"
 if test "$attempt" -eq 1;then usb_busy_owners;fi
 if test "$attempt" -ge 5;then echo 'System USB remains busy. Do not remove USB.';exit 9;fi
 # Allow recently stopped services to release their final open references.
 # Use ordinary unmount on every attempt; never force or detach a busy mount.
 sleep 1
 test "$(awk '$2=="/media/usb0"{print $1}' /proc/mounts)" = "$device" || exit 9
done
! awk -v d="$device" '$1==d{found=1}END{exit !found}' /proc/mounts || exit 10
! losetup -a | grep -q '/media/usb0/' || exit 11
sync
echo SYSTEM_USB_UNMOUNTED_VERIFIED
