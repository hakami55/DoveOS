#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2
cp "$B/backups/moonlight-wide-click-20261005/moonlight-v2-host.sh" "$B/moonlight-v2-host.sh"
sync
echo ORIGINAL_STREAM_INPUT_RESTORED_FOR_NEXT_SESSION
