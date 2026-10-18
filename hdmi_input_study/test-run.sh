#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2/Capture/Test
STATUS=/tmp/doveos-hdmi-test-last.status
LOG=/tmp/doveos-hdmi-test-last.log
test "$#" = 4 || test "$#" = 5
case "$1:$2:$3:$4" in *[!0-9:]*|'') exit 2;;esac
test "$1" -ge 1 && test "$1" -le 255
test "$2" -ge 1 && test "$2" -le 127
test "$3" -le 1
case "$4" in 0|60|120|300) ;;*) exit 2;;esac
profile=${5:-720}
case "$profile" in 720) viewer=tv-viewer;;1080) viewer=tv-viewer-1080;;*) exit 2;;esac
LOCK=/tmp/doveos-hdmi-test.lock
if ! mkdir "$LOCK" 2>/dev/null;then
 owner=$(cat "$LOCK/pid" 2>/dev/null || :)
 case "$owner" in ''|*[!0-9]*) ;;*) if kill -0 "$owner" 2>/dev/null;then echo 'HDMI is already open.' > "$STATUS";exit 3;fi;;esac
 rm -f "$LOCK/pid";rmdir "$LOCK";mkdir "$LOCK"
fi
printf '%s\n' "$$" > "$LOCK/pid"
cleanup(){
 code=$?
 if [ "$code" != 0 ] && grep -q '^Preparing HDMI' "$STATUS" 2>/dev/null;then
  echo 'Could not prepare HDMI. Check the USB capture card and try again.' > "$STATUS"
 fi
 rm -f "$LOCK/pid";rmdir "$LOCK"
}
trap cleanup EXIT
echo 'Preparing HDMI...' > "$STATUS"
(cd "$B";sha256sum -c test-helpers.sha256) > "$LOG" 2>&1
sha256sum -c "$B/tv-platform.sha256" >> "$LOG" 2>&1
/bin/sh /media/usb0/DoveOS-v2/Capture/prepare.sh >> "$LOG" 2>&1
for pair in "$viewer:doveos-hdmi-test-viewer" capture-audio:doveos-capture-audio;do
 source=${pair%:*};target=/tmp/${pair#*:}
 cp "$B/$source" "$target.new";cmp "$B/$source" "$target.new";chmod 700 "$target.new";mv "$target.new" "$target"
done
export ALSA_CONFIG_PATH=/tmp/doveos-uvc-audio-runtime/alsa-minimal.conf
export DOVE_CAPTURE_PRIORITY=1 DOVE_CAPTURE_WORKER_PRIORITY=1 DOVE_CAPTURE_PIPELINE=1 DOVE_CAPTURE_FAST_SCAN=1 DOVE_CAPTURE_HARDWARE=1 DOVE_CAPTURE_DIAGNOSTIC_SHOW_LATE=1
unset DOVE_CAPTURE_PROFILE DOVE_CAPTURE_HALF DOVE_CAPTURE_DUMP DOVE_CAPTURE_NO_AUDIO DOVE_CAPTURE_COPY_DISPLAY DOVE_CAPTURE_ZERO_COPY DOVE_CAPTURE_NORMAL_PRIORITY DOVE_CAPTURE_SINGLE_INPUT DOVE_HDMI_PC_OFFSET_MS DOVE_HDMI_AUDIO_BUFFER_US DOVE_CAPTURE_AUDIT DOVE_CAPTURE_RESET_RING DOVE_CAPTURE_BENCH_AUDIO LD_PRELOAD
test "$3" = 1 || export DOVE_CAPTURE_NO_AUDIO=1
echo 'HDMI is playing.' > "$STATUS"
printf 'HDMI_TEST_PROFILE %sp60 requested_seconds=%s\n' "$profile" "$4" >> "$LOG"
set +e
/tmp/doveos-v2-root/lib/ld-linux-armhf.so.3 --library-path /tmp/doveos-uvc-audio-runtime/lib:/tmp/doveos-v2-root/lib/arm-linux-gnueabihf:/usr/lib:/lib /tmp/doveos-hdmi-test-viewer --watch "$1" "$2" 60 "$4" >> "$LOG" 2>&1
result=$?
set -e
summary=$(awk '/^TV_DONE / {shown=received=rate=0;for(i=1;i<=NF;i++){split($i,v,"=");if(v[1]=="shown")shown=v[2];if(v[1]=="received")received=v[2];if(v[1]=="active_fps")rate=v[2];}if(received>1)printf "Average displayed: %.1f fps.",shown*rate/(received-1);}' "$LOG")
if [ "$result" != 0 ];then
 echo 'HDMI stopped unexpectedly. Check the source and try again.' > "$STATUS"
elif [ "$3" = 0 ];then
 printf 'HDMI closed. %s Sound was switched off.\n' "$summary" > "$STATUS"
elif grep -q '^Audio stopped:.* peak=0 ' "$LOG";then
 printf 'HDMI closed. %s No HDMI sound received; check the PC output and video volume.\n' "$summary" > "$STATUS"
elif grep -q '^Audio stopped:' "$LOG";then
 printf 'HDMI closed. %s HDMI audio received.\n' "$summary" > "$STATUS"
else
 printf 'HDMI closed. %s HDMI sound was unavailable; check the source.\n' "$summary" > "$STATUS"
fi
exit "$result"
