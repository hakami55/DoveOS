#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2
C=$B/Cursors
T=/tmp/doveos-theme
Q=/tmp/doveos-cursors
mkdir -p "$Q" "$B/home/.icons"
(cd "$C";sha256sum -c runtime.sha256)
if [ -r "$B/Memory/static-runtime.sh" ];then /bin/sh "$B/Memory/static-runtime.sh" unseal cursors;fi
gunzip -c "$C/runtime.tar.gz" | tar -xf - -C "$Q"
(cd "$Q";sha256sum -c files.sha256)
chmod 700 "$Q/gpu-cursor" "$Q/doveos-open"
if [ ! -f "$B/home/.icons/DoveClassicCursor/index.theme" ];then
 cp -R "$Q/DoveClassicCursor" "$B/home/.icons/"
fi
mkdir -p "$B/home/.icons/default"
printf '[Icon Theme]\nInherits=DoveClassicCursor\n' > "$B/home/.icons/default/index.theme"
cp "$Q/doveos-open" "$T/doveos-open"
chmod 700 "$T/doveos-open"
if ! grep -q DOVEOS_CLASSIC_CURSOR "$T/doveos-session";then
 sed '1a\
# DOVEOS_CLASSIC_CURSOR\
export XCURSOR_THEME=DoveClassicCursor XCURSOR_SIZE=32 XCURSOR_PATH=/root/.icons:/usr/share/icons' "$T/doveos-session" > "$T/doveos-session.cursor-next"
 mv "$T/doveos-session.cursor-next" "$T/doveos-session"
 chmod 700 "$T/doveos-session"
fi
if ! grep -q 'gtk-cursor-theme-name = "DoveClassicCursor"' "$T/gtkrc";then
 printf '\ngtk-cursor-theme-name = "DoveClassicCursor"\ngtk-cursor-theme-size = 32\n' >> "$T/gtkrc"
fi
if [ -r "$B/Memory/static-runtime.sh" ];then
 /bin/sh "$B/Memory/static-runtime.sh" stage cursors || echo CURSOR_STATIC_RAM_FALLBACK
fi
echo DOVEOS_CLASSIC_CURSOR_THEME_INSTALLED
