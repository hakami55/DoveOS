#!/bin/sh
# Immutable desktop files can use reclaimable read-only USB-backed pages.
set -eu
B=/media/usb0/DoveOS-v2
U=$B/Memory/StaticRuntime
A=/tmp/doveos-static-runtime
M=/tmp/doveos-v2-root
action=${1:?action required}
group=${2:?group required}
case "$group" in
 theme) target=/tmp/doveos-theme;archive=$B/classic-theme.tar.gz;;
 rgb) target=/tmp/doveos-rgb;archive=$B/GPU/rgb/runtime.tar.gz;;
 cursors) target=/tmp/doveos-cursors;archive=$B/Cursors/runtime.tar.gz;;
 *) exit 2;;
esac
case "$action" in unseal|stage) ;;*) exit 2;;esac
# Startup/cleanup owns these directories. Never replace a live GPU's files.
gpu=$(cat /tmp/doveos-gpu-desktop.pid 2>/dev/null || :)
case "$gpu" in ''|*[!0-9]*) :;; *)
 if kill -0 "$gpu" 2>/dev/null;then echo STATIC_RUNTIME_OWNER_BUSY;exit 4;fi;;
esac
test ! -e /tmp/gpu-rgb-retained
test ! -L "$target"
test -d "$target"
if [ "$action" = unseal ];then
 # Remove only links created by this helper before the original tar loader.
 # This also makes disabled/updated packages use the ordinary RAM fallback.
 list=/tmp/doveos-static-unseal-$group.$$
 find "$target" -type l > "$list"
 changed=0
 while IFS= read -r file;do
  case "$(readlink "$file")" in
   "$A/$group/"*) rm "$file";changed=1;;
  esac
 done < "$list"
 rm "$list"
 if [ "$group" = rgb ] && [ "$changed" = 1 ];then rm -f "$target/installed.sha256";fi
 exit 0
fi
test ! -e "$U/DISABLED"
test -r "$U/static-runtime.ext2" && test -r "$U/static-runtime.sha256"
expected=$(awk '{print $1}' "$U/static-runtime.sha256")
case "$expected" in ''|*[!0-9a-f]*) exit 4;;esac
test "${#expected}" = 64
/bin/sh "$B/root-mount-guard.sh" "$B" "$A" "$U/static-runtime.ext2" >/tmp/doveos-static-mount.log 2>&1
loop=$(awk -v p="$A" '$2==p {print $1}' /proc/mounts)
identity=$(/tmp/doveos-loop-image-check "$loop" "$U/static-runtime.ext2")
stamp="$expected $identity"
if [ "$(cat /tmp/doveos-static-runtime.verified 2>/dev/null || :)" != "$stamp" ];then
 (cd "$U";sha256sum -c static-runtime.sha256) >/tmp/doveos-static-image-check.log
 (cd "$A";sha256sum -c files.sha256) >/tmp/doveos-static-files-check.log
 printf '%s\n' "$stamp" > /tmp/doveos-static-runtime.verified
fi
test "$(sha256sum "$archive" | cut -d ' ' -f1)" = "$(cat "$A/$group.source-sha256")"
# /tmp was bound into the chroot before this new submount was created.
if ! awk -v p="$M/tmp/doveos-static-runtime" '$2==p{found=1}END{exit !found}' /proc/mounts;then
 mkdir -p "$M/tmp/doveos-static-runtime"
 mount --bind "$A" "$M/tmp/doveos-static-runtime"
fi
awk -v p="$M/tmp/doveos-static-runtime" -v device="$loop" '$1==device&&$2==p&&$3=="ext2"&&$4~/^ro(,|$)/{n++}END{exit n!=1}' /proc/mounts
# Validate all files before replacing any. Updated runtime files retain RAM.
while read sha name;do
 case "$name" in ''|/*|*..*) exit 4;;esac
 test -f "$target/$name" && test ! -L "$target/$name"
 test "$(sha256sum "$target/$name" | cut -d ' ' -f1)" = "$sha"
 test ! -e "$target/$name.static-next"
 test ! -L "$target/$name.static-next"
done < "$A/$group.sha256"
count=0
while read sha name;do
 ln -s "$A/$group/$name" "$target/$name.static-next"
 mv -f "$target/$name.static-next" "$target/$name"
 count=$((count+1))
done < "$A/$group.sha256"
echo "STATIC_RUNTIME_USB_BACKED group=$group files=$count"
