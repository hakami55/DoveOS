#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2/Backups/xbox-wireless-20261007
test ! -e /tmp/doveos-direct-display || { echo EXIT_STREAM_BEFORE_ROLLBACK >&2;exit 2; }
test "$(sha256sum /media/usb0/autorun/01-drmosa-menu.sh | cut -d ' ' -f1)" = b7bebf068f44151e29511433e8e9b56d5c76fe5aee1e31ae3e1da48f6e73da76 || exit 3
test "$(sha256sum /media/usb0/DrMoSa/settings.sh | cut -d ' ' -f1)" = ff92161a8fef754bf7f16d075a35567ab2bfa1ad637c468d1b4cb9662d133939 || exit 3
test -r "$B/01-drmosa-menu.sh" && test -r "$B/settings.sh"
/bin/sh /media/usb0/DrMoSa/xow/wireless-service.sh stop
cp "$B/01-drmosa-menu.sh" /media/usb0/autorun/01-drmosa-menu.sh.rollback-new
mv -f /media/usb0/autorun/01-drmosa-menu.sh.rollback-new /media/usb0/autorun/01-drmosa-menu.sh
cp "$B/settings.sh" /media/usb0/DrMoSa/settings.sh.rollback-new
mv -f /media/usb0/DrMoSa/settings.sh.rollback-new /media/usb0/DrMoSa/settings.sh
rm -f /media/usb0/DrMoSa/xow/wireless-service.sh /media/usb0/DrMoSa/xow/wireless.sha256
sync
echo WIRELESS_MANUAL_START_RESTORED
