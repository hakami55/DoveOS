#!/bin/sh
# Static runtime files are backed by a verified read-only USB filesystem.
set -eu
B=/media/usb0/DoveOS-v2
U=$B/Memory
A=/tmp/doveos-memory-assets
action=${1:-check}
if [ "$action" = release-legacy ];then
 if [ -d /tmp/drmosa-usb-menu-runtime ];then
  find /tmp/drmosa-usb-menu-runtime -type l | while read file;do
   case "$(readlink "$file")" in "$A"/legacy/*) rm -f "$file";;esac
  done
 fi
 exit 0
fi
test ! -f "$U/DISABLED" || exit 3
test -r "$U/assets.ext2" && test -r "$U/assets.sha256"
expected=$(awk '{print $1}' "$U/assets.sha256")
case "$expected" in ''|*[!0-9a-f]*) exit 4;;esac
test "${#expected}" -eq 64
/bin/sh "$B/root-mount-guard.sh" "$B" "$A" "$U/assets.ext2" > /tmp/doveos-memory-mount.log 2>&1
loop=$(awk -v point="$A" '$2==point{print $1}' /proc/mounts)
identity=$(/tmp/doveos-loop-image-check "$loop" "$U/assets.ext2")
stamp="$expected $identity"
if [ "$(cat /tmp/doveos-memory-assets.verified 2>/dev/null || :)" != "$stamp" ];then
 (cd "$U";sha256sum -c assets.sha256) > /tmp/doveos-memory-image-check.log 2>&1 || exit 4
 grep -q FAILED /tmp/doveos-memory-image-check.log && exit 4
 (cd "$A";sha256sum -c files.sha256) > /tmp/doveos-memory-files-check.log 2>&1 || exit 4
 grep -q FAILED /tmp/doveos-memory-files-check.log && exit 4
 printf '%s\n' "$stamp" > /tmp/doveos-memory-assets.verified
fi
alias_files(){
 manifest=$1;target=$2;source=$3;count=0
 mkdir -p "$target/lib"
 while read sha name;do
  case "$name" in *..*|/*|'') exit 4;;esac
  src=$source/$name;dst=$target/$name
  test -r "$src"
  if [ -L "$dst" ] && [ "$(readlink "$dst")" = "$src" ];then continue;fi
  if [ -e "$dst" ];then
   actual=$(sha256sum "$dst");actual=${actual%% *}
   [ "$actual" = "$sha" ] || { echo "MEMORY_ASSET_VERSION_RETAINED $name";continue; }
  fi
  rm -f "$dst.memory-new"
  ln -s "$src" "$dst.memory-new"
  mv -f "$dst.memory-new" "$dst"
  count=$((count+1))
 done < "$manifest"
 echo "MEMORY_ASSET_LINKS $target $count"
}
case "$action" in
 check) echo MEMORY_ASSETS_VERIFIED;;
 legacy)
  # If the original USB installation was updated, its normal copy path wins.
  (cd /media/usb0/DrMoSa;sha256sum -c "$A/legacy-source.sha256") > /tmp/doveos-memory-source-check.log 2>&1 || exit 3
  grep -q FAILED /tmp/doveos-memory-source-check.log && exit 3
  alias_files "$A/legacy-aliases.txt" /tmp/drmosa-usb-menu-runtime "$A/legacy";;
 debug)
  test -d /tmp/drmosa-moonlight-pair/lib
  alias_files "$A/pair-aliases.txt" /tmp/drmosa-moonlight-pair "$A/legacy";;
 mime)
  # Compare the source archive revision recorded during installation.
  expected_mime=$(cat "$U/mime-archive.sha256")
  actual=$(sha256sum "$B/mime-runtime.tar.gz");actual=${actual%% *}
  test "$actual" = "$expected_mime" || exit 3
  echo "$A/light/mime";;
 *) exit 2;;
esac
