#!/bin/sh
# Sourced coordination for this dock's old Bluetooth controller. Its pending
# LE initiation must finish/cancel before another profile creates a link.
DOVEOS_RADIO_DIR=/tmp/doveos-bluetooth/radio-operation.lock
DOVEOS_RADIO_HELD=0
DOVEOS_RADIO_LE_PENDING=0
DOVEOS_RADIO_LE_ADDRESS=
radio_connected(){
 /usr/local/bin/bluetooth/hcitool con 2>/dev/null | grep -F "$1" | grep -Eq 'state 1([[:space:]]|$)'
}
radio_acquire(){
 dove_radio_n=0
 while [ "$dove_radio_n" -lt "${1:-30}" ];do
  if mkdir "$DOVEOS_RADIO_DIR" 2>/dev/null;then
   dove_radio_start=$(awk '{print $22}' "/proc/$$/stat")
   printf '%s %s\n' "$$" "$dove_radio_start" >"$DOVEOS_RADIO_DIR/owner" || return 1
   DOVEOS_RADIO_HELD=1
   return 0
  fi
  dove_radio_owner=$(cat "$DOVEOS_RADIO_DIR/owner" 2>/dev/null || :)
  dove_radio_pid=${dove_radio_owner%% *}
  dove_radio_stamp=${dove_radio_owner#* }
  case "$dove_radio_pid" in
   '') [ "$dove_radio_n" -lt 2 ] || rmdir "$DOVEOS_RADIO_DIR" 2>/dev/null || :;;
   *[!0-9]*) ;;
   *)
    dove_radio_live=$(awk '{print $22}' "/proc/$dove_radio_pid/stat" 2>/dev/null || :)
    if [ "$dove_radio_live" != "$dove_radio_stamp" ] &&
       [ "$(cat "$DOVEOS_RADIO_DIR/owner" 2>/dev/null || :)" = "$dove_radio_owner" ];then
     rm -f "$DOVEOS_RADIO_DIR/owner"
     rmdir "$DOVEOS_RADIO_DIR" 2>/dev/null || :
    fi;;
  esac
  dove_radio_n=$((dove_radio_n+1));sleep 1
 done
 return 1
}
radio_cancel_pending(){
 if [ "$DOVEOS_RADIO_HELD" = 1 ] && [ "$DOVEOS_RADIO_LE_PENDING" = 1 ];then
  if ! radio_connected "$DOVEOS_RADIO_LE_ADDRESS";then
   # LE Create Connection Cancel, issued only for our locked initiation.
   /usr/local/bin/bluetooth/hcitool cmd 0x08 0x000e >>"${DOVEOS_RADIO_LOG:-/tmp/doveos-bluetooth/radio-cancel.log}" 2>&1 || :
  fi
  DOVEOS_RADIO_LE_PENDING=0
 fi
}
radio_release(){
 [ "$DOVEOS_RADIO_HELD" = 1 ] || return 0
 radio_cancel_pending
 dove_radio_owner=$(cat "$DOVEOS_RADIO_DIR/owner" 2>/dev/null || :)
 case "$dove_radio_owner" in "$$ "*)
  rm -f "$DOVEOS_RADIO_DIR/owner"
  rmdir "$DOVEOS_RADIO_DIR" 2>/dev/null || :;;
 esac
 DOVEOS_RADIO_HELD=0
}
