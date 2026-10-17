#!/bin/sh
# Owned USB-C dock Ethernet; all runtime files and drivers stay in RAM.
set -u
PATH=/usr/bin:/bin:/usr/sbin:/sbin;export PATH
R=/tmp/doveos-rtl8153
child=;guard=;owns=0
owned_old(){ test "$(cat /sys/class/net/axdebug0/address 2>/dev/null)" = 00:1e:b5:01:de:8c; }
has_address(){ ip -o -4 addr show dev "$1" 2>/dev/null | grep -q '169.254.77.2/16'; }
publish(){ printf '%s\n' "$1" > "$R/network.next";mv "$R/network.next" "$R/network"; }
restore_old(){
 if test "$owns" = 1;then
  if has_address rtdebug0;then ip addr del 169.254.77.2/16 dev rtdebug0 || :;fi
  if owned_old;then
   has_address axdebug0 || ip addr add 169.254.77.2/16 dev axdebug0 || :
   arping -U -I axdebug0 -c 2 -w 2 169.254.77.2 >/dev/null 2>&1 || :
  fi
  owns=0
 fi
 publish 'fallback'
}
stop_child(){
 test -n "$child" || return 0
 if test "$(readlink /proc/$child/exe 2>/dev/null)" = "$R/ecm-bridge";then
  kill -TERM "$child" 2>/dev/null || :
  for n in 1 2 3 4;do kill -0 "$child" 2>/dev/null || break;sleep 1;done
  kill -KILL "$child" 2>/dev/null || :
 fi
 wait "$child" 2>/dev/null || :;child=
}
stop_guard(){
 if test -n "$guard";then
  test "$(readlink /proc/$guard/exe 2>/dev/null)" != /usr/local/etc/drmosa-ethernet/keepawake || kill -TERM "$guard" 2>/dev/null || :
  wait "$guard" 2>/dev/null || :;guard=
 fi
}
finish(){ trap - EXIT INT TERM;restore_old;stop_child;stop_guard;publish stopped;rm -f "$R/service.pid"; }
trap finish EXIT
trap 'exit 0' INT TERM
echo $$ > "$R/service.pid"
valid_guard=0
G=/usr/local/etc/drmosa-ethernet
if test "$(sha256sum /usr/local/bin/IMS_Modules/wfd_dfb | cut -d ' ' -f1)" = cb3b26154ce5f07113c614a2d7f03c59ac113333340f96966c31f9e7cdb295c2 && (cd "$G" && sha256sum -c SHA256SUMS >/dev/null 2>&1);then valid_guard=1;fi
while test ! -f "$R/stop";do
 present=0
 for d in /sys/bus/usb/devices/[0-9]*;do
  if test "$(cat "$d/idVendor" 2>/dev/null)" = 0bda && test "$(cat "$d/idProduct" 2>/dev/null)" = 8153;then present=1;break;fi
 done
 if test "$present" = 0;then publish 'adapter absent';sleep 3;continue;fi
 guard_attempted=0
 rm -f /tmp/doveos-ecm-status
 "$R/ecm-bridge" --service > "$R/bridge.log" 2>&1 & child=$!
 echo "$child" > "$R/driver.pid"
 while kill -0 "$child" 2>/dev/null && test ! -f "$R/stop";do
  state=$(awk '{print $3}' "/proc/$child/stat" 2>/dev/null)
  test "$state" != Z || break
  if test "$valid_guard" = 1 && test -e /sys/class/net/rtdebug0;then
   if test "$(cat "$G/keepawake-enabled" 2>/dev/null)" != 1;then stop_guard;guard_attempted=0
   elif test "$guard_attempted" = 0 && test -z "$guard" && test "$(cat /sys/class/gpio/gpio67/value 2>/dev/null)" = 0;then
    "$G/keepawake" "$R/keepawake.status" /tmp/drmosa-keepawake.lock > "$R/keepawake.log" 2>&1 & guard=$!;guard_attempted=1
   fi
  fi
  if test -n "$guard" && test ! -r "/proc/$guard/cmdline";then wait "$guard" 2>/dev/null || :;guard=;fi
  status=$(cat /tmp/doveos-ecm-status 2>/dev/null)
  case "$status" in "pid=$child "*)
   link=$(printf '%s\n' "$status" | sed -n 's/.* link=\([-0-9]*\) .*/\1/p')
   rate=$(printf '%s\n' "$status" | sed -n 's/.* down_bps=\([0-9]*\).*/\1/p')
   if test "$link" = 1 && test "$(cat /sys/class/net/rtdebug0/address 2>/dev/null)" = 00:e0:4c:68:0c:fb;then
    if ! has_address rtdebug0;then
     owns=1
     if owned_old && has_address axdebug0;then ip addr del 169.254.77.2/16 dev axdebug0 || :;fi
     if ip addr add 169.254.77.2/16 dev rtdebug0;then
      owns=1
      arping -U -I rtdebug0 -c 2 -w 2 169.254.77.2 >/dev/null 2>&1 || :
     else restore_old;fi
    else owns=1;fi
    publish "rtdebug0 connected ${rate:-0}"
   elif test "$link" = 0;then restore_old;publish 'rtdebug0 cable disconnected'
   else publish 'rtdebug0 waiting for cable';fi
   ;;
  esac
  sleep 2
 done
 restore_old;stop_child;stop_guard;rm -f "$R/driver.pid"
 test ! -f "$R/stop" || break
 sleep 5
done
