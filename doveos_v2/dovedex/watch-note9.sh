#!/bin/sh
# Exit DeX closes the receiver; a failed status read does not imply DeX exited.
set -eu
R=/tmp/dovedex-runtime-v1
adbname=$1;port=$2;serial=$3;owner=$4
case "$adbname:$port" in adb-usb:5040|adb-wifi:5039) ;;*) exit 2;;esac
case "$owner" in ''|*[!0-9]*) exit 2;;esac
disabled=0
while kill -0 "$owner" 2>/dev/null && [ "$(cat "$R/session-lock/owner" 2>/dev/null || :)" = "$owner" ];do
 state=$("$R/dovedex-run" 8 "$R/$adbname" -P "$port" -s "$serial" shell 'timeout 3 dumpsys desktopmode | grep -E "^ *mState=|^ *mDesktopDisplayId="' 2>/dev/null | tr -d '\r' || :)
 if printf '%s\n' "$state" | grep '^ *mState=State' >/dev/null &&
    { printf '%s\n' "$state" | grep -F 'SemDesktopModeState(DISABLED,' >/dev/null ||
      printf '%s\n' "$state" | grep '^ *mDesktopDisplayId=-1 *$' >/dev/null; };then
  disabled=$((disabled+1))
 else
  disabled=0
 fi
 if [ "$disabled" -ge 2 ];then
  if [ "$(cat "$R/session-lock/owner" 2>/dev/null || :)" = "$owner" ];then
   echo ANDROID_DEVICE_DEX_EXIT_DETECTED
   touch "$R/session.stop"
  fi
  exit 0
 fi
 sleep 2
done
