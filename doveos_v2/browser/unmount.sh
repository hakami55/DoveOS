#!/bin/sh
set -eu
L=/tmp/doveos-webkit-root
Q=/tmp/browser-webkit-work
mounted(){ awk -v p="$1" '$2==p{ok=1}END{exit !ok}' /proc/mounts; }
for p in "$L/tmp/mesa" "$L/dev/shm" "$L/dev/null" "$L/dev/zero" "$L/dev/random" "$L/dev/urandom" "$L/dev" "$L/proc" "$L/etc" "$L/tmp" "$L" "$Q/.X11-unix" "$Q/mesa" /tmp/modern-browser-webkit-direct /tmp/modern-browser-mesa-direct;do
 if mounted "$p";then umount "$p";fi
done
