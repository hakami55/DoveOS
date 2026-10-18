#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2/Capture/Test
(cd "$B";sha256sum -c test-helpers.sha256) > /tmp/doveos-hdmi-test-ui-integrity.log
for pair in hdmi-test-ui:doveos-hdmi-test-ui receiver-run:doveos-hdmi-test-run;do
 source=${pair%:*};target=/tmp/${pair#*:}
 cp "$B/$source" "$target.new";cmp "$B/$source" "$target.new";chmod 700 "$target.new";mv "$target.new" "$target"
done
/tmp/doveos-hdmi-test-run --exec chroot /proc/1/root /bin/sh /media/usb0/DoveOS-v2/Capture/prepare.sh >> /tmp/doveos-hdmi-test-ui-integrity.log 2>&1
export LD_LIBRARY_PATH=/tmp/doveos-uvc-audio-runtime/lib:${LD_LIBRARY_PATH:-/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf}
exec /tmp/doveos-hdmi-test-run --exec /tmp/doveos-hdmi-test-ui
