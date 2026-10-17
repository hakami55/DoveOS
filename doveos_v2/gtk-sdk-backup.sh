set -e
B=/media/usb0/DoveOS-v2
mkdir -p "$B/backups/before-gtk-sdk-20261001"
cp "$B/classic-theme.tar.gz" "$B/apply-theme.sh" "$B/start-desktop.sh" "$B/backups/before-gtk-sdk-20261001/"
echo GTK_SDK_BACKUP_OK
