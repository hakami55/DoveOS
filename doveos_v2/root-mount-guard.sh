#!/bin/sh
# Invoked before desktop children start. Only a read-only ext2 loop image with
# this exact backing pathname can be reused or detached. No block-device writes.
set -eu
root=${1:-/media/usb0/DoveOS-v2}
point=${2:-/tmp/doveos-v2-root}
image=${3:-$root/doveos-v2.ext2}
checker=/tmp/doveos-loop-image-check
test -f "$image"
expected=$(awk '{print $1}' "$root/loop-image-check.sha256")
test "$(sha256sum "$root/loop-image-check" | awk '{print $1}')" = "$expected" || { echo LOOP_CHECK_HASH_FAILED;exit 4; }
if ! cmp -s "$root/loop-image-check" "$checker";then
 cp "$root/loop-image-check" "$checker.new";chmod 700 "$checker.new"
 mv "$checker.new" "$checker"
fi
mounted=$(awk -v point="$point" '$2==point{print $1}' /proc/mounts)
if [ -n "$mounted" ];then
 case "$mounted" in /dev/loop*) loop=${mounted#/dev/};;*) echo ROOT_MOUNT_UNEXPECTED_DEVICE;exit 4;;esac
 case "${loop#loop}" in ''|*[!0-9]*) exit 4;;esac
 awk -v point="$point" '$2==point&&$3=="ext2"&&$4~/^ro(,|$)/{n++}END{exit n!=1}' /proc/mounts || { echo ROOT_MOUNT_NOT_READ_ONLY;exit 4; }
 status=0
 identity=$("$checker" "$mounted" "$image") || status=$?
 echo "$identity"
 test "$status" -le 1 || exit 4
 backing=$(cat "/sys/block/$loop/loop/backing_file")
 if [ "$backing" != "$image" ];then
  # Removed USB mounts lose their /media/usb0 prefix in sysfs. Accept only
  # these known image paths, with a stale identity and an absent original
  # block device; renamed/foreign files and attached devices remain rejected.
  case "$image" in
   /media/usb0/DoveOS-v2/doveos-v2.ext2|/media/usb0/DoveOS-v2/optional-apps.ext2|/media/usb0/DoveOS-v2/light-apps.ext2|/media/usb0/DoveOS-v2/Memory/assets.ext2|/media/usb0/DoveOS-v2/Memory/StaticRuntime/static-runtime.ext2|/media/usb0/DoveOS-v2/GPU/desktop/runtime.ext2|/media/usb0/DoveOS-v2/AirPlay/doveos-airplay.ext2|/media/usb0/DoveOS-v2/Capture/userspace-uvc.ext2|/media/usb0/DoveOS-v2/Capture/userspace-uvc-audio.ext2)
    test "$backing" = "${image#/media/usb0}" && test "$status" -eq 1 &&
     echo "$identity" | grep -q 'backing_device_present=0' || { echo ROOT_MOUNT_WRONG_IMAGE;exit 4; };;
   *) echo ROOT_MOUNT_WRONG_IMAGE;exit 4;;
  esac
 fi
 if [ "$status" -eq 0 ];then
  echo DOVEOS_V2_REUSING_VERIFIED_ROOT_MOUNT
 else
  # Refuse to detach a live desktop chroot. Native audio may still retain
  # mappings from the old read-only image; namespace detach preserves them.
  for directory in /proc/[0-9]*;do
   if [ "$(readlink "$directory/root" 2>/dev/null || :)" = "$point" ];then
    echo ROOT_STALE_CHROOT_BUSY;exit 4
   fi
  done
  # Refuse child mounts; normal desktop cleanup must remove them first.
  if awk -v prefix="$point/" 'index($2,prefix)==1{found=1}END{exit !found}' /proc/mounts;then
   echo ROOT_STALE_CHILD_MOUNTS_BUSY;exit 4
  fi
  umount "$point" || umount -l "$point"
  echo DOVEOS_V2_STALE_READONLY_ROOT_DETACHED
  mounted=
 fi
fi
if [ -z "$mounted" ];then
 mkdir -p "$point"
 mount -t ext2 -o loop,ro "$image" "$point"
fi
mounted=$(awk -v point="$point" '$2==point{print $1}' /proc/mounts)
"$checker" "$mounted" "$image"
echo DOVEOS_V2_ROOT_IDENTITY_VERIFIED
