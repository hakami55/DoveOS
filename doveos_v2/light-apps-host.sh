#!/bin/sh
set -e
app=${1:-};shift || exit 2
case "$app" in
 netsurf) program=/usr/bin/netsurf-gtk;;
 abiword) program=/usr/bin/abiword;;
 mupdf) program=/usr/lib/mupdf/mupdf-x11;;
 *) exit 2;;
esac
/bin/sh /media/usb0/DoveOS-v2/light-apps-mount.sh >> /tmp/doveos-light-mount.log 2>&1
unset GTK_PATH LD_PRELOAD GIO_EXTRA_MODULES
export HOME=/root DISPLAY=:7 LANG=C.UTF-8
export LD_LIBRARY_PATH=/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf
export XDG_CONFIG_HOME=/root/.config XDG_DATA_HOME=/root/.local/share XDG_CACHE_HOME=/tmp/doveos-light-cache
export GDK_BACKEND=x11 GTK_THEME=Chicago95 GSETTINGS_BACKEND=memory
export GDK_PIXBUF_MODULE_FILE=/tmp/doveos-light-pixbuf.cache
export XDG_RUNTIME_DIR=/tmp/doveos-v2-runtime
exec chroot /tmp/doveos-light-root "$program" "$@"
