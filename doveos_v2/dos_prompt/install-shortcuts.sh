#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2
T=/tmp/doveos-theme
M=/tmp/doveos-v2-root
cp "$B/Terminal/Terminal.desktop" "$T/live-etc/Terminal.desktop"
chmod 755 "$T/live-etc/Terminal.desktop"
mkdir -p "$B/home/.local/share/applications"
cp "$B/Terminal/Terminal.desktop" "$B/home/.local/share/applications/Dove-terminal.desktop"
for file in "$T/jwmrc" "$T/live-etc/jwmrc" "$T/keyboard-action.sh";do
 test -f "$file" || continue
 sed -i -e 's|/tmp/doveos-theme/doveos-open xterm xterm|/bin/sh /media/usb0/DoveOS-v2/Terminal/launch.sh|g' -e 's|label="DOS Prompt"|label="Terminal"|g' "$file"
done
if [ "${1:-}" = --live ];then
 cp "$B/Terminal/Terminal.desktop" "$M/root/Desktop/Terminal.desktop"
 chmod 755 "$M/root/Desktop/Terminal.desktop"
 cp "$T/live-etc/jwmrc" "$M/root/.jwmrc"
 chroot "$M" /usr/bin/env DISPLAY=:7 /usr/bin/jwm -reload
fi
