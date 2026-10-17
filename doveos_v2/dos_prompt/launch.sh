#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2/Terminal
R=/tmp/doveos-terminal-runtime
mkdir -p "$R"
(cd "$B";sha256sum -c files.sha256) >/tmp/doveos-terminal-check.log 2>&1
# Install the console font once on persistent home storage; keep system fonts.
F="/root/.local/share/fonts"
mkdir -p "$F"
if ! cmp -s "$B/MorePerfectDOSVGA.ttf" "$F/MorePerfectDOSVGA.ttf";then
 cp "$B/MorePerfectDOSVGA.ttf" "$F/MorePerfectDOSVGA.ttf"
 fc-cache "$F"
fi
for name in doveos-dos-prompt libz.so.1;do
 expected=$(awk -v name="$name" '$2==name{print $1}' "$B/files.sha256")
 cp "$B/$name" "$R/$name.new.$$"
 actual=$(sha256sum "$R/$name.new.$$")
 test "${actual%% *}" = "$expected"
 chmod 700 "$R/$name.new.$$"
 mv "$R/$name.new.$$" "$R/$name"
done
export HOME=/root DISPLAY="${DISPLAY:-:7}"
export GTK_PATH=/tmp/doveos-theme/gtk-modules
export GDK_PIXBUF_MODULE_FILE=/tmp/doveos-v2-pixbuf.cache
export LD_LIBRARY_PATH="$R:/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf"
exec "$R/doveos-dos-prompt" "$@"
