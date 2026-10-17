#!/bin/sh
# Used on a replacement DoveOS USB after restoring the memory update archive.
# It creates only the named blank swap file, and refuses to overwrite one.
set -eu
U=/media/usb0/DoveOS-v2/Memory
test ! -e "$U/swap.bin"
test -r "$U/swap-blank.bin.gz"
test -r "$U/swap-blank.sha256"
test "$(df -k /media/usb0 | tail -1 | awk '{print $4}')" -gt 262144
gunzip -c "$U/swap-blank.bin.gz" > "$U/swap.bin.new"
test "$(ls -ln "$U/swap.bin.new" | awk '{print $5}')" = 134217728
expected=$(cat "$U/swap-blank.sha256")
actual=$(sha256sum "$U/swap.bin.new");actual=${actual%% *}
test "$actual" = "$expected"
mv "$U/swap.bin.new" "$U/swap.bin"
sync
echo BLANK_SWAP_RESTORED
