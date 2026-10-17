#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
RUNTIME=/home/hakami55/doveos-v2-build/desktop-runtime
test -f "$RUNTIME/.ready"
rsync -a "$RUNTIME/" "$1/"
rsync -a "$HERE/overlay/" "$1/"
find "$1/usr/bin" -maxdepth 1 -name 'doveos-*' -exec chmod 755 {} +
rm -f "$1/.ready"
