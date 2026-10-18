#!/bin/sh
# Verified UI overlay; the base runtime and hardware services stay intact.
set -eu
B=/media/usb0/DoveOS-v2/UIRefresh
T=/tmp/doveos-theme
(cd "$B";sha256sum -c files.sha256) >/tmp/doveos-ui-refresh-check.log
case "${1:-}" in
 theme)
  # The system USB is mounted noexec. Load the verified executable into RAM.
  cp "$B/doveos-settings-ui" "$T/doveos-settings-ui.ui-refresh-next"
  cmp "$B/doveos-settings-ui" "$T/doveos-settings-ui.ui-refresh-next"
  chmod 755 "$T/doveos-settings-ui.ui-refresh-next"
  mv -f "$T/doveos-settings-ui.ui-refresh-next" "$T/doveos-settings-ui"
  ;;
 menu)
  for menu in "$T/live-etc/jwmrc" "$T/jwmrc" /tmp/doveos-v2-root/root/.jwmrc;do
   test -f "$menu" || continue
   sed '/<Program .*\/usr\/bin\/doveos-settings-ui power<\/Program>/s|icon="[^"]*"|icon="/media/usb0/DoveOS-v2/UIRefresh/power-24.png"|' "$menu" > "$menu.ui-refresh-next"
   cat "$menu.ui-refresh-next" > "$menu"
   rm -f "$menu.ui-refresh-next"
  done
  ;;
 *) exit 2;;
esac
echo DOVEOS_UI_REFRESH_APPLIED
