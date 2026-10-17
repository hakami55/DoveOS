set -e
ROOT=/media/usb0/DoveOS-v2
mkdir -p "$ROOT/backups/before-menu-spacing"
for file in classic-theme.tar.gz apply-theme.sh start-desktop.sh;do
 [ -f "$ROOT/backups/before-menu-spacing/$file" ] || cp "$ROOT/$file" "$ROOT/backups/before-menu-spacing/$file"
done
sync
