#!/bin/sh
# Shared saved-connection lifecycle for supported BLE input devices.
# Pairing chooses a device; this monitor maintains input without re-pairing.
set -u
umask 077
U=/media/usb0/DrMoSa/Bluetooth
B=/tmp/doveos-bluetooth
action=${1:-}
kind=${2:-}
address=${3:-}
case "$kind" in
 mouse) H=$B/hid; helper=$U/bluetooth-input.sh; session=mouse-session.sh; pidfile=mouse.pid; title=Mouse;;
 keyboard) H=$B/keyboard-hid; helper=$U/bluetooth-keyboard.sh; session=keyboard-session.sh; pidfile=keyboard.pid; title=Keyboard;;
 *) exit 2;;
esac
D=$B/input-services/$kind
mkdir -p "$D"
valid(){ printf '%s\n' "$1" | grep -Eq '^[[:xdigit:]]{2}(:[[:xdigit:]]{2}){5}$'; }
owned(){
 candidate=$1; needle=$2
 case "$candidate" in ''|*[!0-9]*) return 1;; esac
 [ -r "/proc/$candidate/cmdline" ] &&
 tr '\000' ' ' <"/proc/$candidate/cmdline" | grep -Fq "$needle"
}
linked(){
 /usr/local/bin/bluetooth/hcitool con 2>/dev/null |
 grep -F "$1" | grep -Eq 'state 1([[:space:]]|$)'
}
ready(){
 [ -f "$H/ready" ] &&
 owned "$(cat "$H/$pidfile" 2>/dev/null)" "$H/$session $1 " && linked "$1"
}
state(){ printf '%s\n' "$1" >"$D/state.new";mv "$D/state.new" "$D/state"; }
case "$action" in
 start)
  valid "$address" || exit 2
  printf '%s\n' "$address" >"$D/desired.new";mv "$D/desired.new" "$D/desired"
  if ! owned "$(cat "$D/service.pid" 2>/dev/null)" "$D/service.sh run $kind";then
   cp "$U/bluetooth-hid-service.sh" "$D/service.sh.new" && mv "$D/service.sh.new" "$D/service.sh" || exit 1
   nohup /bin/sh "$D/service.sh" run "$kind" >"$D/service.log" 2>&1 </dev/null &
  fi
  for n in 1 2 3 4 5 6 7 8;do
   if ready "$address";then
    printf 'Bluetooth %s connected. Desktop input ready.\n' "$kind" >"$B/result";exit 0
   fi
   sleep 1
  done
  printf 'Bluetooth %s paired; connecting input automatically. Keep it awake.\n' "$kind" >"$B/result"
  exit 0;;
 stop)
  valid "$address" || exit 2
  desired=$(cat "$D/desired" 2>/dev/null || :)
  active=$(cat "$D/active" 2>/dev/null || :)
  [ "$desired" = "$address" ] || [ "$active" = "$address" ] || exit 0
  if [ "$desired" = "$address" ];then : >"$D/desired.new";mv "$D/desired.new" "$D/desired";fi
  for n in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40;do
   active=$(cat "$D/active" 2>/dev/null || :)
   [ "$active" = "$address" ] || exit 0
   if ! owned "$(cat "$D/service.pid" 2>/dev/null)" "$D/service.sh run $kind";then
    DOVEOS_HID_RESULT="$D/attempt-result" /bin/sh "$helper" stop "$address" || exit 1
    rm -f "$D/active";exit 0
   fi
   sleep 1
  done
  printf 'Bluetooth %s input is still stopping; retry Forget shortly.\n' "$kind" >"$B/result"
  exit 1;;
 run)
  if ! mkdir "$D/runner.lock" 2>/dev/null;then
   if owned "$(cat "$D/service.pid" 2>/dev/null)" "$D/service.sh run $kind";then exit 0;fi
   rmdir "$D/runner.lock" 2>/dev/null || exit 1
   mkdir "$D/runner.lock" || exit 1
  fi
  echo $$ >"$D/service.pid"
  active=
  cleanup(){
   trap - EXIT INT TERM HUP
   if valid "$active";then DOVEOS_HID_RESULT="$D/attempt-result" /bin/sh "$helper" stop "$active" >/dev/null 2>&1 || :;fi
   rm -f "$D/service.pid" "$D/active"
   rmdir "$D/runner.lock" 2>/dev/null || :
  }
  trap cleanup EXIT
  trap 'exit 0' INT TERM HUP
  while :;do
   desired=$(cat "$D/desired" 2>/dev/null || :)
   if [ "$desired" != "$active" ];then
    if valid "$active";then DOVEOS_HID_RESULT="$D/attempt-result" /bin/sh "$helper" stop "$active" >/dev/null 2>&1 || :;fi
    active=$desired
    if valid "$active";then printf '%s\n' "$active" >"$D/active";else rm -f "$D/active";fi
   fi
   if ! valid "$active";then state 'Idle';sleep 2;continue;fi
   if ready "$active";then state "Ready $active";sleep 2;continue;fi
   # gatttool can stay alive after link loss. Stop it to release held input
   # and remove stale readiness before making another authenticated attempt.
   state "Reconnecting $active"
   DOVEOS_HID_RESULT="$D/attempt-result" /bin/sh "$helper" stop "$active" >/dev/null 2>&1 || :
   rm -f "$H/ready"
   printf '%s disconnected; reconnecting automatically.\n' "$title" >"$H/state"
   # Do not run the UI action script here: it saves bonds and shares result
   # files. The selected-device helper uses its own private result file.
   DOVEOS_HID_RESULT="$D/attempt-result" /bin/sh "$helper" start "$active" >"$D/attempt.log" 2>&1 || :
   if ready "$active";then state "Ready $active";else state "Waiting $active";sleep 3;fi
  done;;
 *) exit 2;;
esac
