#!/bin/sh
# Restore the previous USB startup scripts. Keep backing assets for the live
# session; the original RAM layout returns after the next normal dock reboot.
set -eu
B=/media/usb0/DoveOS-v2
U=$B/Memory
archive=$U/rollback-scripts.tar
test -f "$archive"
test "$(sha256sum "$archive" | cut -d' ' -f1)" = 241af6a944ad1c16ee2fe11f1fbe553a6bc4c9c2a08b6e5fe027e751a6ce735d
if [ -r "$U/swap-runtime.sh" ];then /bin/sh "$U/swap-runtime.sh" stop;fi
touch "$U/DISABLE_SWAP" "$U/DISABLED"
tar -xf "$archive" -C /media/usb0
sync
echo PREVIOUS_USB_SCRIPTS_RESTORED
echo 'Current desktop remains running. Original RAM layout returns on the next normal dock reboot.'
