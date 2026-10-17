set -e
ROOT=/media/usb0
SOURCE=$ROOT/autorun/01-drmosa-menu.sh
mkdir -p "$ROOT/DoveOS-v2/backups/before-restart-fix"
[ -f "$ROOT/DoveOS-v2/backups/before-restart-fix/01-drmosa-menu.sh" ] || cp "$SOURCE" "$ROOT/DoveOS-v2/backups/before-restart-fix/01-drmosa-menu.sh"
if ! grep -q DOVEOS_V2_STARTUP_GUARD "$SOURCE";then
 sed '/        if pidof jwm /i\        # DOVEOS_V2_STARTUP_GUARD: do not launch v1 during v2 startup.\n        if [ -f /tmp/doveos-v2.pid ] && kill -0 "$(cat /tmp/doveos-v2.pid)" 2>/dev/null;then restore_autorun;exit 0;fi' "$SOURCE" > "$SOURCE.new"
 /bin/sh -n "$SOURCE.new"
 mv "$SOURCE.new" "$SOURCE"
fi
sync
echo RESTART_AND_AUTORUN_FIXED
