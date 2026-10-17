#!/bin/sh
# Called inside the desktop. Keep the original CPU app as the fallback.
if [ "${DISPLAY:-}" = :7 ] && [ -S /tmp/doveos-rgb/native-windows.sock ];then
 sh /media/usb0/DoveOS-v2/GPU/native-window/gpu-shared-run.sh --in-desktop --mouse-test "$@"
 result=$?
 test "$result" != 0 || exit 0
fi
exec /tmp/doveos-theme/doveos-mouse-test-cpu "$@"
