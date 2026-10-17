#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2
file=$B/Memory/browser-swap.bin
active(){ awk -v p="$file" '$1==p{ok=1}END{exit !ok}' /proc/swaps; }
case "${1:-status}" in
 start)
  /bin/sh "$B/Memory/swap-runtime.sh" start
  if active;then exit 0;fi
  test -f "$file" && test ! -L "$file"
  test "$(ls -ln "$file" | awk '{print $5}')" = 268435456
  expected=$(awk '{print $1}' "$B/Memory/browser-swap-header.sha256")
  actual=$(dd if="$file" bs=4096 count=1 2>/dev/null | sha256sum);actual=${actual%% *}
  test "$expected" = "$actual"
  /tmp/modern-browser-tools/swap-on;;
 stop) if active;then swapoff "$file";fi;;
 status) cat /proc/swaps;;
 *) exit 2;;
esac
