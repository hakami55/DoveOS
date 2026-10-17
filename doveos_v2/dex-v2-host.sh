#!/bin/sh
# USB/RAM-only handoff. Never replaces the installed receiver executable.
set -u
ROOT=/media/usb0/DoveOS-v2
RUNTIME=/tmp/drmosa-usb-menu-runtime
LOG="$ROOT/dex.log"
receiver= monitor= input= watchdog= route_applied=0 mouse_route_applied=0
native(){ "$RUNTIME/lib/ld-linux-armhf.so.3" --library-path "$RUNTIME/lib:/usr/lib:/lib" "$@"; }
status(){ printf '%s\n' "$1" > /tmp/doveos-dex-status.next; mv /tmp/doveos-dex-status.next /tmp/doveos-dex-status; }
restore(){
 trap - EXIT INT TERM HUP
 if [ -n "$input" ];then
  kill "$input" 2>/dev/null || :
  n=0
  while kill -0 "$input" 2>/dev/null && [ "$n" -lt 3 ];do n=$((n+1));sleep 1;done
  kill -9 "$input" 2>/dev/null || :
 fi
 if [ "$mouse_route_applied" -eq 1 ];then
  native /tmp/doveos-dex-keyboard-route restore-mouse "$receiver" >> "$LOG" 2>&1 || :
 fi
 if [ "$route_applied" -eq 1 ];then
  native /tmp/doveos-dex-keyboard-route restore "$receiver" >> "$LOG" 2>&1 || :
 fi
 [ -z "$watchdog" ] || kill "$watchdog" 2>/dev/null || :
 [ -z "$monitor" ] || kill "$monitor" 2>/dev/null || :
 if [ -n "$monitor" ];then
  ticks=0
  while kill -0 "$monitor" 2>/dev/null && [ "$ticks" -lt 3 ];do ticks=$((ticks+1));sleep 1;done
  kill -9 "$monitor" 2>/dev/null || :
 fi
 wpa_cli -i p2p0 disconnect >/tmp/doveos-dex-disconnect.log 2>&1 || :
 # The HP receiver tears down its video plane asynchronously after disconnect.
 # Let it finish before the desktop allocates and starts its own video buffers.
 echo DEX_WAITING_FOR_VIDEO_TEARDOWN >> "$LOG"
 sleep 6
 printf 'bt\n' >/proc/aeiudp
 rm -f /tmp/doveos-dex.stop /tmp/doveos-dex-host.pid /tmp/doveos-dex-input.ready
 status "Returned to DoveOS"
 echo DEX_HANDOFF_ENDED >> "$LOG"
 sync
}
start_input(){
 [ "$(sha256sum /usr/local/bin/IMS_Modules/wfd_dfb | cut -d ' ' -f 1)" = cb3b26154ce5f07113c614a2d7f03c59ac113333340f96966c31f9e7cdb295c2 ] || return 1
 cp "$ROOT/dex-input" /tmp/doveos-dex-input || return 1
 cp "$ROOT/dex-keyboard-route" /tmp/doveos-dex-keyboard-route || return 1
 chmod 700 /tmp/doveos-dex-input /tmp/doveos-dex-keyboard-route
 native /tmp/doveos-dex-input --check >> "$LOG" 2>&1 || return 1
 native /tmp/doveos-dex-keyboard-route apply "$receiver" >> "$LOG" 2>&1 || return 1
 route_applied=1
 native /tmp/doveos-dex-keyboard-route apply-mouse "$receiver" >> "$LOG" 2>&1 || return 1
 mouse_route_applied=1
 printf 'bt\n' >/proc/aeiudp
 rm -f /tmp/doveos-dex-input.ready
 "$RUNTIME/lib/ld-linux-armhf.so.3" --library-path "$RUNTIME/lib:/usr/lib:/lib" /tmp/doveos-dex-input --hid-mouse >> "$LOG" 2>&1 &
 input=$!
 n=0
 while [ ! -f /tmp/doveos-dex-input.ready ] && [ "$n" -lt 3 ];do
  kill -0 "$input" 2>/dev/null || return 1
  n=$((n+1));sleep 1
 done
 test -f /tmp/doveos-dex-input.ready || return 1
 # Independent guard restores process memory and releases input on a host crash.
 cat >/tmp/doveos-dex-session-watchdog.sh <<'EOF'
owner=$1;receiver=$2;input=$3
while kill -0 "$owner" 2>/dev/null && [ "$(cat /tmp/doveos-dex-host.pid 2>/dev/null)" = "$owner" ];do sleep 2;done
kill "$input" 2>/dev/null || :
sleep 1
kill -9 "$input" 2>/dev/null || :
R=/tmp/drmosa-usb-menu-runtime
"$R/lib/ld-linux-armhf.so.3" --library-path "$R/lib" /tmp/doveos-dex-keyboard-route restore-mouse "$receiver"
"$R/lib/ld-linux-armhf.so.3" --library-path "$R/lib" /tmp/doveos-dex-keyboard-route restore "$receiver"
printf 'bt\n' >/proc/aeiudp
EOF
 nohup sh /tmp/doveos-dex-session-watchdog.sh "$$" "$receiver" "$input" >> "$LOG" 2>&1 </dev/null &
 watchdog=$!
 return 0
}
receiver=$(pidof wfd_dfb 2>/dev/null || :)
case "$receiver" in ''|*[!0-9]*) status "HP receiver unavailable - restart the dock";exit 1;;esac
test -r "$ROOT/moonlight-quit-combo" || exit 2
test -x "$RUNTIME/lib/ld-linux-armhf.so.3" || exit 3
echo DEX_HANDOFF_STARTED > "$LOG"
echo $$ > /tmp/doveos-dex-host.pid
rm -f /tmp/doveos-dex.stop
trap restore EXIT INT TERM HUP
cp "$ROOT/moonlight-quit-combo" /tmp/doveos-dex-quit
chmod 700 /tmp/doveos-dex-quit
"$RUNTIME/lib/ld-linux-armhf.so.3" --library-path "$RUNTIME/lib" /tmp/doveos-dex-quit --stop-file /tmp/doveos-dex.stop >> "$LOG" 2>&1 &
monitor=$!
status "Waiting for Wireless DeX - select HP Lap Dock on your phone"
echo "RECEIVER_PID=$receiver reused_stock_receiver=1" >> "$LOG"
waited=0;seen=0;missing=0;mode_fixups=6
while kill -0 "$receiver" 2>/dev/null && [ ! -f /tmp/doveos-dex.stop ];do
 if netstat -an 2>/dev/null | grep 'ESTABLISHED' | grep -e ':7236' -e ':25030' >/dev/null;then
  if [ "$seen" -eq 0 ];then
   if start_input;then status "Wireless DeX connected - keyboard and touchpad enabled";echo DEX_CONNECTED_INPUT_ADAPTER >> "$LOG"
   else status "DeX input adapter unavailable - returning to DoveOS";echo DEX_INPUT_START_FAILED >> "$LOG";break;fi
  fi
  seen=1;missing=0
 elif [ "$seen" -eq 1 ];then
  missing=$((missing+2));[ "$missing" -lt 12 ] || break
 else
  waited=$((waited+2));[ "$waited" -lt 180 ] || { echo DEX_WAIT_TIMEOUT >> "$LOG";break; }
 fi
 if [ -n "$input" ] && ! kill -0 "$input" 2>/dev/null;then echo DEX_INPUT_PROCESS_ENDED >> "$LOG";break;fi
 # The stock receiver switches to mouse mode after RTSP becomes established.
 # Preserve ABS touchpad input through its short, asynchronous initialization.
 if [ "$seen" -eq 1 ] && [ "$mode_fixups" -gt 0 ];then
  printf 'bt\n' >/proc/aeiudp
  mode_fixups=$((mode_fixups-1))
 fi
 sleep 2
done
echo "DEX_EXIT connected=$seen waited=$waited disconnected=$missing" >> "$LOG"
