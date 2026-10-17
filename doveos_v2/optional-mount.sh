#!/bin/sh
# Mount optional application binaries without replacing the DoveOS root image.
set -e
BASE=/media/usb0/DoveOS-v2
IMAGE=$BASE/optional-apps.ext2
M=/tmp/doveos-optional-root
test -f "$IMAGE" || exit 0
# Validate loop identity even when old page-cache reads still succeed.
mounted=$(awk -v point="$M" '$2==point{print $1}' /proc/mounts)
needs_mount=1
if [ -n "$mounted" ];then
 checker=/tmp/doveos-loop-image-check
 expected=$(awk '{print $1}' "$BASE/loop-image-check.sha256")
 test "$(sha256sum "$BASE/loop-image-check" | awk '{print $1}')" = "$expected" || exit 4
 if ! cmp -s "$BASE/loop-image-check" "$checker";then
  cp "$BASE/loop-image-check" "$checker.new";chmod 700 "$checker.new";mv "$checker.new" "$checker"
 fi
 if "$checker" "$mounted" "$IMAGE";then
  current_usb=$(awk '$2=="/media/usb0"{print $1}' /proc/mounts)
  optional_usb=$(awk -v point="$M/media/usb0" '$2==point{print $1}' /proc/mounts)
  if [ "$current_usb" = "$optional_usb" ] && [ -r "$M/usr/bin/vlc" ];then needs_mount=0;fi
 else
  result=$?;test "$result" -eq 1 || exit 4
 fi
fi
if [ "$needs_mount" -eq 1 ];then
 for directory in /proc/[0-9]*;do
  if [ "$(readlink "$directory/root" 2>/dev/null || :)" = "$M" ];then echo OPTIONAL_RUNTIME_BUSY;exit 2;fi
 done
 if [ -n "$mounted" ];then
  for path in media/usb0 tmp sys proc dev/shm dev;do umount "$M/$path" 2>/dev/null || :;done
 fi
 /bin/sh "$BASE/root-mount-guard.sh" "$BASE" "$M" "$IMAGE"
 mount --bind /dev "$M/dev"
 mount --bind /dev/shm "$M/dev/shm"
 mount --bind /proc "$M/proc"
 mount --bind /sys "$M/sys"
 mount --bind /tmp "$M/tmp"
 mount --bind /media/usb0 "$M/media/usb0"
fi

test -r "$M/usr/bin/vlc" || { echo OPTIONAL_RUNTIME_UNREADABLE;exit 2; }
mkdir -p /tmp/doveos-optional-home /tmp/doveos-optional-runtime
chown 65534:29 /tmp/doveos-optional-home /tmp/doveos-optional-runtime
chmod 700 /tmp/doveos-optional-runtime
cp "$BASE/optional-chroot" /tmp/doveos-optional-chroot
chmod 700 /tmp/doveos-optional-chroot
if [ -f "$BASE/vlc-pcm-bridge" ];then
 if ! cmp -s "$BASE/vlc-pcm-bridge" /tmp/doveos-vlc-pcm-bridge;then
  cp "$BASE/vlc-pcm-bridge" /tmp/doveos-vlc-pcm-bridge
 fi
 chmod 700 /tmp/doveos-vlc-pcm-bridge
fi
if [ -f "$BASE/vlc-rtk-host" ] && [ -f "$BASE/libdove_rtk_plugin.so" ];then
 mkdir -p /tmp/doveos-vlc-plugins/codec
 chmod 755 /tmp/doveos-vlc-plugins /tmp/doveos-vlc-plugins/codec
 for pair in vlc-rtk-host:doveos-vlc-rtk-host libdove_rtk_plugin.so:doveos-vlc-plugins/codec/libdove_rtk_plugin.so vlc-rc-client:doveos-vlc-rc-client;do
  source=${pair%%:*};target=${pair#*:}
  if ! cmp -s "$BASE/$source" "/tmp/$target";then
   cp "$BASE/$source" "/tmp/$target.new"
   chmod 755 "/tmp/$target.new"
   mv "/tmp/$target.new" "/tmp/$target"
  fi
 done
fi
echo OPTIONAL_APPS_MOUNTED
