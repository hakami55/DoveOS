#!/bin/sh
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
WORK=${DOVEOS_BUILD_DIR:-/home/hakami55/doveos-v2-build}
VERSION=2025.02.18
BR="$WORK/buildroot-$VERSION"
mkdir -p "$WORK" "$HERE/artifacts"
if [ ! -d "$BR" ]; then
    curl -fLsS --connect-timeout 15 --max-time 120 "https://buildroot.org/downloads/buildroot-$VERSION.tar.xz" -o "$WORK/buildroot.tar.xz"
    tar -xJf "$WORK/buildroot.tar.xz" -C "$WORK"
fi
make -C "$BR" O="$WORK/windowed-output" BR2_EXTERNAL="$HERE" doveos_v2_defconfig
make -C "$BR" O="$WORK/windowed-output" BR2_EXTERNAL="$HERE" -j8
cp "$WORK/windowed-output/images/rootfs.ext2" "$HERE/artifacts/doveos-v2.ext2"
gzip -1 -c "$HERE/artifacts/doveos-v2.ext2" > "$HERE/artifacts/doveos-v2.ext2.gz"
cp "$WORK/windowed-output/.config" "$HERE/artifacts/buildroot.config"
sha256sum "$HERE"/artifacts/doveos-v2.ext2 > "$HERE/artifacts/SHA256SUMS"
echo DOVEOS_V2_BUILD_READY
