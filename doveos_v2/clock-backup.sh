set -e
ROOT=/media/usb0/DoveOS-v2
BACK=$ROOT/backups/before-clock-and-tray
mkdir -p "$BACK"
for file in classic-theme.tar.gz apply-theme.sh start-desktop.sh; do
 [ -f "$ROOT/$file" ] && [ -f "$BACK/$file" ] || cp "$ROOT/$file" "$BACK/$file"
done
sync
echo CLOCK_TRAY_BACKUP_SAVED
