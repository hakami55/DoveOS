#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2/YouTubeTrial
(cd "$B";sha256sum -c receiver.sha256) >/dev/null
for pair in youtube-ui:doveos-youtube-ui receiver-run:doveos-youtube-run;do
 source=${pair%:*};target=/tmp/${pair#*:}
 cp "$B/$source" "$target.new";cmp "$B/$source" "$target.new"
 chmod 700 "$target.new";mv "$target.new" "$target"
done
exec /tmp/doveos-youtube-run --exec /tmp/doveos-youtube-ui "$@"
