#!/bin/sh
# Direct, fully allocated USB swap. No loop device or internal-flash writes.
set -eu
# Always operate in the stock host root. Starting from a USB/SDK chroot can
# publish a different swap path and leave an old mount active after reconnect.
if [ ! -r /usr/local/bin/IMS_Modules/wfd_dfb ];then
 host_hash=$(sha256sum /proc/1/root/usr/local/bin/IMS_Modules/wfd_dfb 2>/dev/null | cut -d ' ' -f 1)
 [ "$host_hash" = cb3b26154ce5f07113c614a2d7f03c59ac113333340f96966c31f9e7cdb295c2 ] || exit 4
 exec /usr/sbin/chroot /proc/1/root /bin/sh /media/usb0/DoveOS-v2/Memory/swap-runtime.sh "$@"
fi
U=/media/usb0/DoveOS-v2/Memory
file=$U/swap.bin
browser=$U/browser-swap.bin
saved=/tmp/doveos-memory-swappiness.before
active(){ awk -v path="$file" '$1==path{found=1}END{exit !found}' /proc/swaps; }
foreign_active(){ awk -v path="$file" -v browser="$browser" '$1!=path&&$1!=browser&&$1~/\/DoveOS-v2\/Memory\/(browser-)?swap\.bin$/{found=1}END{exit !found}' /proc/swaps; }
case "${1:-status}" in
 start)
  if foreign_active;then echo DOVEOS_SWAP_OTHER_MOUNT_ACTIVE;exit 4;fi
  [ -f "$U/ENABLE_SWAP" ] && [ ! -f "$U/DISABLE_SWAP" ] || exit 0
  if active;then echo DOVEOS_SWAP_ALREADY_ACTIVE;exit 0;fi
  test -f "$file" && test ! -L "$file"
  test "$(awk '$2=="/media/usb0"{print $3}' /proc/mounts)" = vfat
  test "$(ls -ln "$file" | awk '{print $5}')" = 134217728
  expected=$(cat "$U/swap-header.sha256")
  actual=$(dd if="$file" bs=4096 count=1 2>/dev/null | sha256sum);actual=${actual%% *}
  test "$actual" = "$expected" || { echo SWAP_HEADER_MISMATCH;exit 4; }
  swapon "$file"
  active || exit 4
  if [ ! -r "$saved" ];then cat /proc/sys/vm/swappiness > "$saved";fi
  echo 10 > /proc/sys/vm/swappiness
  echo DOVEOS_USB_SWAP_ENABLED_128_MIB_SWAPPINESS_10;;
 stop)
  if foreign_active;then echo DOVEOS_SWAP_OTHER_MOUNT_ACTIVE_EJECT_REFUSED;exit 4;fi
  if awk -v p="$browser" '$1==p{ok=1}END{exit !ok}' /proc/swaps;then swapoff "$browser";fi
  # Refuse to proceed with an eject if the kernel cannot page everything in.
  if active;then swapoff "$file";fi
  if active;then echo DOVEOS_SWAP_STILL_ACTIVE;exit 4;fi
  if [ -r "$saved" ];then
   value=$(cat "$saved")
   case "$value" in ''|*[!0-9]*) exit 4;;esac
   test "$value" -le 100
   if [ "$(cat /proc/sys/vm/swappiness)" = 10 ];then echo "$value" > /proc/sys/vm/swappiness;fi
   rm -f "$saved"
  fi
  echo DOVEOS_USB_SWAP_STOPPED;;
 status)
  cat /proc/swaps
  grep -E 'MemTotal:|MemFree:|SwapTotal:|SwapFree:|Shmem:' /proc/meminfo
  printf 'Swappiness: ';cat /proc/sys/vm/swappiness;;
 *) exit 2;;
esac
