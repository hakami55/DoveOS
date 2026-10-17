#!/bin/sh
set -eu
B=/mnt/c/vscode/HPlapdock/airplay_receiver_study
sh "$B/build_hw_bridge.sh"
python3 "$B/pack-doveos-runtime.py"
truncate -s 160M "$B/doveos-airplay.ext2"
mke2fs -q -F -t ext2 -b 4096 -d /home/hakami55/doveos-airplay-runtime-20261005 "$B/doveos-airplay.ext2"
e2fsck -fn "$B/doveos-airplay.ext2"
(cd "$B";sha256sum doveos-airplay.ext2 > image.sha256)
du -sk /home/hakami55/doveos-airplay-runtime-20261005
