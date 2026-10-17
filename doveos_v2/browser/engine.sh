#!/bin/sh
set -eu
Q=/tmp/browser-webkit-work
P=/media/usb0/DoveOS-v2/ModernBrowser
echo $$ > "$Q/engine.pid"
export WEBKIT_DISABLE_COMPOSITING_MODE=1 WEBKIT_DISABLE_DMABUF_RENDERER=1
# ARMv7 baseline and DFG JIT passed calculation checks and real-page trials.
# FTL remains off. DOVE_BROWSER_JIT=0 provides an interpreter recovery mode.
export JSC_useJIT=true JSC_useDFGJIT=true JSC_useFTLJIT=false
if [ "${DOVE_BROWSER_JIT:-1}" = 0 ];then export JSC_useJIT=false JSC_useDFGJIT=false;fi
unset GTK_MODULES GTK_PATH GTK_THEME
export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=softpipe LIBGL_DRIVERS_PATH=/tmp/mesa/dri
exec /tmp/modern-browser-tools/browser-run 0 /tmp/doveos-webkit-root /usr/bin/dbus-run-session -- /usr/bin/surf -T -e "$1" "${2:-https://example.org/}" >> "$Q/browser.log" 2>&1
