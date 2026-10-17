#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2
M=/tmp/doveos-v2-root
R=/tmp/doveos-power-button
PID=/tmp/doveos-power-button.pid
owned_pid(){
 [ -r "$PID" ] || return 1
 old=$(cat "$PID")
 case "$old" in ''|*[!0-9]*) return 1;;esac
 [ -r "/proc/$old/cmdline" ] || return 1
 cmd=$(tr '\000' ' ' < "/proc/$old/cmdline")
 case "$cmd" in *"$R --service "*) return 0;;*) return 1;;esac
}
case "${1:-start}" in
 stop)
  if owned_pid;then kill "$old" 2>/dev/null || :;fi
  exit 0;;
 start) ;;
 *) exit 2;;
esac
if owned_pid;then echo POWER_BUTTON_ALREADY_ACTIVE;exit 0;fi
[ ! -e "$B/home/disable-power-button-sleep" ] || exit 0
test "$(sha256sum /usr/local/bin/IMS_Modules/wfd_dfb | cut -d' ' -f1)" = cb3b26154ce5f07113c614a2d7f03c59ac113333340f96966c31f9e7cdb295c2
expected=3eda617241261372a6503956c70a5cd704a6edeb2001cbefa9f0e4a69111b0ec
test "$(sha256sum "$B/power-button-guard" | cut -d' ' -f1)" = "$expected"
# A previous stopping service may be waiting for the physical awake level.
# Do not bind over an existing GPIO override or acquire a second raw handle.
if grep -q gpio80 /proc/self/mountinfo;then echo POWER_BUTTON_RESTORE_PENDING;exit 3;fi
cp "$B/power-button-guard" "$R.new.$$"
test "$(sha256sum "$R.new.$$" | cut -d' ' -f1)" = "$expected"
chmod 700 "$R.new.$$";mv "$R.new.$$" "$R"
# The ITE reader initializes asynchronously on a cold boot. Wait for its
# validated status before the guard attempts to mask the stock GPIO lookup.
ticks=0
while [ "$ticks" -lt 12 ];do
 read up idle < /proc/uptime
 if [ -r /tmp/doveos-backlight-state ] && awk -v current="$up" '$1=="DOVE_BACKLIGHT1" && $3>=3 && $3<=100 && $4>=0 && $4<=2 && current-$2>=-0.02 && current-$2<3 {ready=1} END{exit !ready}' /tmp/doveos-backlight-state;then break;fi
 sleep 1;ticks=$((ticks+1))
done
[ "$ticks" -lt 12 ] || { echo POWER_BUTTON_BACKLIGHT_NOT_READY;exit 4; }
"$M/lib/ld-linux-armhf.so.3" --library-path "$M/usr/lib/arm-linux-gnueabihf:$M/lib/arm-linux-gnueabihf" "$R" --service > "$B/power-button.log" 2>&1 </dev/null &
echo $! > "$PID"
sleep 1
grep GUARD_READY "$B/power-button.log"
