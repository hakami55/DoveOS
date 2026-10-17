#!/bin/sh
# The bridge invokes only these allowlisted desktop actions.
set -eu
M=/tmp/doveos-v2-root
export DISPLAY=:7 HOME=/root
case "${1:-}" in
 settings) exec chroot "$M" /tmp/doveos-theme/doveos-open DoveSettings --page 8 /usr/bin/doveos-settings-ui keyboard;;
 files) exec chroot "$M" /bin/sh /media/usb0/DoveOS-v2/Explorer/launch.sh /media/usb0/DrMoSa/Documents;;
 terminal) exec chroot "$M" /tmp/doveos-theme/doveos-open xterm xterm;;
esac
exit 2
