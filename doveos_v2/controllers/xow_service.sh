#!/bin/sh
# Manage the previously tested Xbox Wireless Adapter driver from RAM.
set -u
umask 077
B=/media/usb0/DrMoSa/xow
R=/tmp/drmosa-xow
SELF=/tmp/doveos-xow-service.sh
PID=/tmp/doveos-xow-service.pid
LOCK=/tmp/doveos-xow-service.lock
LIB=/tmp/drmosa-usb-menu-runtime/lib
valid_pid(){ case "$1" in ''|*[!0-9]*) return 1;;esac;test "$1" -gt 1; }
manager_running(){
 m=$(cat "$PID" 2>/dev/null || :)
 valid_pid "$m" && test -r /proc/$m/cmdline || return 1
 args=$(tr '\000' ' ' < /proc/$m/cmdline)
 test "$args" = "/bin/sh $SELF run " && kill -0 "$m" 2>/dev/null
}
driver_running(){
 p=$(cat "$R/pid" 2>/dev/null || :)
 valid_pid "$p" && test -r /proc/$p/cmdline || return 1
 args=$(tr '\000' ' ' < /proc/$p/cmdline)
 case "$args" in "$LIB/ld-linux-armhf.so.3 --library-path $R:$LIB $R/xow ") ;;*) return 1;;esac
 kill -0 "$p" 2>/dev/null
}
dongle_present(){
 for d in /sys/bus/usb/devices/*;do
  test -r "$d/idVendor" && test -r "$d/idProduct" || continue
  if [ "$(cat "$d/idVendor")" = 045e ] && [ "$(cat "$d/idProduct")" = 02e6 ];then return 0;fi
 done
 return 1
}
stop_driver(){
 if driver_running;then
  kill -TERM "$p" 2>/dev/null || :
  n=0;while driver_running && [ "$n" -lt 5 ];do sleep 1;n=$((n+1));done
  driver_running && return 1
 fi
 rm -f "$R/pid"
}
cleanup(){
 trap - EXIT INT TERM
 stop_driver || :
 test "$(cat "$PID" 2>/dev/null || :)" != "$$" || rm -f "$PID"
 rmdir "$LOCK" 2>/dev/null || :
}
case "${1:-start}" in
 start)
  test ! -e /tmp/doveos-shutting-down && test ! -f "$B/DISABLED" || exit 0
  manager_running && exit 0
  (cd "$B" && sha256sum -c wireless.sha256) >/tmp/doveos-xow-check.log 2>&1 || exit 2
  mkdir -p "$R" || exit 2
  for f in xow firmware.bin libstdc++.so.6;do
   cp "$B/$f" "$R/$f.new.$$" || exit 2
   expected=$(awk -v f="$f" '$2==f{print $1}' "$B/wireless.sha256")
   actual=$(sha256sum "$R/$f.new.$$");actual=${actual%% *}
   test "$actual" = "$expected" || exit 3
   chmod 700 "$R/$f.new.$$"
   mv -f "$R/$f.new.$$" "$R/$f" || exit 2
  done
  cp "$B/wireless-service.sh" "$SELF.new.$$" || exit 2
  expected=$(awk '$2=="wireless-service.sh"{print $1}' "$B/wireless.sha256")
  actual=$(sha256sum "$SELF.new.$$");actual=${actual%% *}
  test "$actual" = "$expected" || exit 3
  chmod 700 "$SELF.new.$$";mv -f "$SELF.new.$$" "$SELF" || exit 2
  nohup /bin/sh "$SELF" run </dev/null >>/tmp/doveos-xow-service.log 2>&1 &
  ;;
 run)
  mkdir "$LOCK" 2>/dev/null || exit 0
  echo $$ > "$PID"
  trap cleanup EXIT
  trap 'exit 0' INT TERM
  while test ! -e /tmp/doveos-shutting-down;do
   if ! driver_running && dongle_present;then
    # Retain the last session log; restart only after the previous process exits.
    test ! -r "$R/xow.log" || tail -c 32768 "$R/xow.log" > "$R/previous.log"
    : > "$R/xow.log"
    (cd "$R" && exec "$LIB/ld-linux-armhf.so.3" --library-path "$R:$LIB" "$R/xow") </dev/null >>"$R/xow.log" 2>&1 &
    echo $! > "$R/pid"
    echo XOW_DRIVER_STARTED
    sleep 5
    driver_running || { echo XOW_START_RETRY_DELAY; sleep 25; }
   fi
   if test -r "$R/xow.log";then
    size=$(wc -c < "$R/xow.log")
    if test "$size" -gt 65536;then tail -c 32768 "$R/xow.log" > "$R/log-tail";cat "$R/log-tail" > "$R/xow.log";rm -f "$R/log-tail";fi
   fi
   sleep 2
  done
  ;;
 stop)
  if manager_running;then
   kill -TERM "$m" 2>/dev/null || :
   n=0;while manager_running && [ "$n" -lt 8 ];do sleep 1;n=$((n+1));done
   manager_running && { echo XOW_STOP_PENDING >&2;exit 4; }
  else stop_driver || exit 4;fi
  ;;
 pair)
  driver_running || { echo XOW_NOT_RUNNING >&2;exit 1; }
  kill -USR1 "$p"
  ;;
 toggle)
  if manager_running || driver_running;then
   touch "$B/DISABLED" || exit 2
   /bin/sh "$B/wireless-service.sh" stop || exit $?
   echo 'Xbox wireless driver disabled.' > /tmp/drmosa-settings-result
  else
   rm -f "$B/DISABLED"
   /bin/sh "$B/wireless-service.sh" start || exit $?
   echo 'Xbox wireless driver enabled. Use the sync buttons to pair.' > /tmp/drmosa-settings-result
  fi
  sync
  ;;
 status)
  if manager_running;then echo "XOW_MANAGER_RUNNING pid=$m";else echo XOW_MANAGER_STOPPED;fi
  if driver_running;then echo "XOW_DRIVER_RUNNING pid=$p";else echo XOW_DRIVER_STOPPED;fi
  ;;
 *) exit 2;;
esac
