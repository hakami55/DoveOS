#!/bin/sh
# Verified, reclaimable GPU files. Failure leaves the existing RAM loader available.
set -eu
B=/media/usb0/DoveOS-v2/GPU/desktop
R=/media/usb0/DoveOS-v2
A=/tmp/doveos-gpu-assets
P=/tmp/doveos-gpu-desktop
expected=${1:?runtime archive digest required}
test ! -f "$B/ASSETS_DISABLED"
test -r "$B/runtime.ext2" && test -r "$B/image.sha256"
image_hash=$(awk '{print $1}' "$B/image.sha256")
case "$image_hash" in ''|*[!0-9a-f]*) exit 4;;esac
test "${#image_hash}" -eq 64
/bin/sh "$R/root-mount-guard.sh" "$R" "$A" "$B/runtime.ext2"
loop=$(awk -v point="$A" '$2==point{print $1}' /proc/mounts)
identity=$(/tmp/doveos-loop-image-check "$loop" "$B/runtime.ext2")
stamp="$image_hash $identity"
if [ "$(cat /tmp/doveos-gpu-assets.verified 2>/dev/null || :)" != "$stamp" ];then
 (cd "$B";sha256sum -c image.sha256)
 (cd "$A";sha256sum -c files.sha256)
 test "$(cat "$A/installed.sha256")" = "$expected"
 printf '%s\n' "$stamp" > /tmp/doveos-gpu-assets.verified
fi
test "$(cat "$A/installed.sha256")" = "$expected"
# /tmp was bound into the chroot before this image was mounted. Explicitly
# expose this read-only submount; an ordinary bind does not propagate it.
M=/tmp/doveos-v2-root
target=$M/tmp/doveos-gpu-assets
umount "$target" 2>/dev/null || :
mount --bind "$A" "$target"
awk -v point="$target" '$2==point&&$3=="ext2"&&$4~/^ro(,|$)/{n++}END{exit n!=1}' /proc/mounts
if [ -L "$P" ];then
 test "$(readlink "$P")" = "$A" || exit 4
elif [ -d "$P" ];then
 # The desktop calls prepare only after the previous GPU processes have stopped.
 # Retain the previous runtime for rollback until the new desktop is verified.
 test ! -e "$P.ram-rollback"
 mv "$P" "$P.ram-rollback"
 ln -s "$A" "$P"
else
 test ! -e "$P"
 ln -s "$A" "$P"
fi
echo GPU_ASSETS_READONLY_VERIFIED
