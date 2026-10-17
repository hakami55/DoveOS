#!/bin/sh
# Compatibility entry point used by existing Lap Dock early-start hooks.
set -eu
U=/media/usb0
if [ ! -e "$U/autorun" ] && [ -d "$U/autorun.old" ];then
 mv "$U/autorun.old" "$U/autorun"
fi
if [ -f /tmp/doveos-v2.pid ] && kill -0 "$(cat /tmp/doveos-v2.pid)" 2>/dev/null;then
 exit 0
fi
exec /bin/sh "$U/autorun/01-doveos.sh"
