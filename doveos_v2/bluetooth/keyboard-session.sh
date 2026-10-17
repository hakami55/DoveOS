#!/bin/sh
set -eu
H=/tmp/doveos-bluetooth/keyboard-hid
M=/tmp/doveos-v2-root
address=$1
mode=${2:-native}
input_needle="$H/keyboard-input"
transport= input=
stop_child(){
 pid=$1;needle=$2
 if [ -n "$pid" ] && [ -r "/proc/$pid/cmdline" ] && tr '\000' ' ' <"/proc/$pid/cmdline" | grep -Fq "$needle";then kill -TERM "$pid" 2>/dev/null || :;fi
 [ -z "$pid" ] || wait "$pid" 2>/dev/null || :
}
cleanup(){
 trap - EXIT INT TERM HUP
 stop_child "$transport" "/usr/bin/gatttool -i hci0 -b $address"
 stop_child "$input" "$input_needle"
 rm -f "$H/reports.fifo" "$H/ready"
 echo 'Keyboard disconnected. Wake it and press Reconnect.' >"$H/state"
}
trap cleanup EXIT
trap 'exit 143' INT TERM HUP
if [ "$mode" = dbus ];then
 input_needle="$H/keyboard-gatt"
 DISPLAY=:7 "$M/lib/ld-linux-armhf.so.3" --library-path "$M/usr/lib/arm-linux-gnueabihf:$M/lib/arm-linux-gnueabihf" "$H/keyboard-gatt" "$address" >"$H/keyboard.log" 2>&1 &
 input=$!
 wait "$input" || :
 exit 0
fi
rm -f "$H/reports.fifo"
mkfifo "$H/reports.fifo"
LD_PRELOAD="$H/line-buffer.so" /usr/bin/gatttool -i hci0 -b "$address" -t public -l medium --char-write-req -a 0x0020 -n 0100 --listen >"$H/reports.fifo" 2>"$H/transport.log" &
transport=$!
DISPLAY=:7 "$M/lib/ld-linux-armhf.so.3" --library-path "$M/usr/lib/arm-linux-gnueabihf:$M/lib/arm-linux-gnueabihf" "$H/keyboard-input" <"$H/reports.fifo" >"$H/keyboard.log" 2>&1 &
input=$!
wait "$input" || :
