#!/bin/sh
# Build a separate read-only ARMHF trial image. Never replaces the DoveOS image.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
WORK=/home/hakami55/doveos-v2-build/optional-apps
ROOT=$WORK/root
OUT=$HERE/artifacts/optional-apps.ext2
mkdir -p "$ROOT" "$HERE/artifacts"
rsync -a --exclude='usr/share/doc' --exclude='usr/share/man' \
  --exclude='usr/share/locale' --exclude='usr/lib/arm-linux-gnueabihf/dri' \
  --exclude='usr/lib/arm-linux-gnueabihf/libLLVM*' \
  "$WORK/vlc/" "$ROOT/"
rsync -a --exclude='usr/share/doc' --exclude='usr/share/man' \
  --exclude='usr/share/locale' "$WORK/firefox/" "$ROOT/"
mkdir -p "$ROOT/usr/share/fonts/truetype" "$ROOT/etc/ssl" "$ROOT/dev" \
  "$ROOT/proc" "$ROOT/sys" "$ROOT/tmp" "$ROOT/media/usb0" "$ROOT/root"
rsync -a /home/hakami55/doveos-v2-build/desktop-runtime/usr/share/fonts/truetype/dejavu \
  "$ROOT/usr/share/fonts/truetype/"
rsync -a /etc/ssl/certs "$ROOT/etc/ssl/"
printf 'root:x:0:0:root:/root:/bin/sh\nnobody:x:65534:65534:nobody:/tmp:/bin/sh\n' > "$ROOT/etc/passwd"
printf 'root:x:0:\nnogroup:x:65534:\n' > "$ROOT/etc/group"
printf 'hosts: files dns\n' > "$ROOT/etc/nsswitch.conf"
printf '127.0.0.1 localhost\n' > "$ROOT/etc/hosts"
chmod 1777 "$ROOT/tmp"
echo ROOT_SIZE
du -sh "$ROOT"
rm -f "$OUT"
truncate -s 640M "$OUT"
mke2fs -q -t ext2 -F -d "$ROOT" "$OUT"
e2fsck -fn "$OUT" | tail -5
sha256sum "$OUT" > "$HERE/artifacts/optional-apps.sha256"
echo OPTIONAL_IMAGE_READY
