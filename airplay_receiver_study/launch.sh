#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2/AirPlay
(cd "$B";sha256sum -c helpers.sha256) > /tmp/doveos-airplay-launch-check.log
for pair in airplay-ui:doveos-airplay-ui receiver-run:doveos-airplay-run;do
 source=${pair%:*};target=/tmp/${pair#*:}
 cp "$B/$source" "$target.new";cmp "$B/$source" "$target.new"
 chmod 700 "$target.new";mv "$target.new" "$target"
done
exec /tmp/doveos-airplay-run --exec /tmp/doveos-airplay-ui
