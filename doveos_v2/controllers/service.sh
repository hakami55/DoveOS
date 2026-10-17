#!/bin/sh
# Verified USB package; the running service and log stay in RAM.
set -eu
B=/media/usb0/DoveOS-v2/Controllers
RAM=/tmp/doveos-xbox-usb
PID=/tmp/doveos-xbox-usb.pid
running(){
 test -r "$PID" || return 1
 p=$(cat "$PID")
 case "$p" in ''|*[!0-9]*) return 1;;esac
 test "$p" -gt 1 && test -r "/proc/$p/cmdline" || return 1
 args=$(tr '\000' ' ' < "/proc/$p/cmdline")
 test "$args" = "$RAM --daemon " && kill -0 "$p" 2>/dev/null
}
case "${1:-start}" in
 start)
  test ! -f /tmp/doveos-shutting-down && test ! -f "$B/DISABLED" || exit 0
  running && exit 0
  mkdir /tmp/doveos-xbox-usb-start.lock 2>/dev/null || exit 0
  trap 'rmdir /tmp/doveos-xbox-usb-start.lock 2>/dev/null || :' EXIT
  (cd "$B";sha256sum -c bridge.sha256) >/tmp/doveos-xbox-usb-check.log 2>&1 || exit 2
  cp "$B/doveos-xbox-usb" "$RAM.new.$$"
  expected=$(awk '{print $1}' "$B/bridge.sha256")
  actual=$(sha256sum "$RAM.new.$$");actual=${actual%% *}
  if [ "$actual" != "$expected" ];then rm -f "$RAM.new.$$";exit 3;fi
  chmod 700 "$RAM.new.$$"
  mv -f "$RAM.new.$$" "$RAM"
  nohup "$RAM" --daemon </dev/null >/tmp/doveos-xbox-usb.log 2>&1 &
  echo $! > "$PID"
  ;;
 stop)
  if running;then
   kill -TERM "$p"
   n=0
   while running && [ "$n" -lt 3 ];do sleep 1;n=$((n+1));done
   running && { echo XBOX_STOP_PENDING >&2;exit 4; }
  fi
  rm -f "$PID"
  ;;
 status)
  if running;then echo "XBOX_SERVICE_RUNNING pid=$p";else echo XBOX_SERVICE_STOPPED;exit 1;fi
  ;;
 *) exit 2;;
esac
