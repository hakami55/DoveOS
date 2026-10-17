#!/bin/sh
# Manual preview receiver. Nothing is started at boot.
set -eu
exec 2>&1
B=/media/usb0/DoveOS-v2/AirPlay
R=/tmp/doveos-airplay-runtime
M=/tmp/doveos-v2-root
LOCK=/tmp/doveos-airplay.lock
STOP=/tmp/doveos-airplay.stop
case "${1:-start}" in
 stop) touch "$STOP";exit 0;;
 check|start) ;;
 *) exit 2;;
esac
echo 'Checking installed AirPlay files...'
if ! test "$(sha256sum /usr/local/bin/IMS_Modules/wfd_dfb | cut -d' ' -f1)" = cb3b26154ce5f07113c614a2d7f03c59ac113333340f96966c31f9e7cdb295c2;then
 echo 'This is not the verified Lap Dock software. AirPlay was not started.';exit 2
fi
if ! sh /media/usb0/DoveOS-v2/root-mount-guard.sh /media/usb0/DoveOS-v2 "$R" "$B/doveos-airplay.ext2" > /tmp/doveos-airplay-install-check.log 2>&1;then
 echo 'The DoveOS USB or AirPlay runtime is unavailable. Check the flash drive and try again.';exit 3
fi
loop=$(awk -v point="$R" '$2==point{print $1}' /proc/mounts)
if ! identity=$(/tmp/doveos-loop-image-check "$loop" "$B/doveos-airplay.ext2" 2>>/tmp/doveos-airplay-install-check.log);then
 echo 'The mounted AirPlay runtime does not match the connected USB. Restart DoveOS with its flash drive attached.';exit 3
fi
stamp="$(cat "$B/image.sha256") $identity"
if [ "$(cat /tmp/doveos-airplay.verified 2>/dev/null || :)" != "$stamp" ];then
 if ! (cd "$B";sha256sum -c image.sha256 >> /tmp/doveos-airplay-install-check.log 2>&1) ||
    ! (cd "$R";sha256sum -c files.sha256 > /tmp/doveos-airplay-files.log 2>&1);then
  echo 'AirPlay runtime verification failed. Repair or reinstall the AirPlay files before retrying.';exit 3
 fi
 printf '%s\n' "$stamp" > /tmp/doveos-airplay.verified
fi
export GST_PLUGIN_PATH="$R/plugins" GST_PLUGIN_SYSTEM_PATH="$R/plugins"
export GST_REGISTRY=/tmp/doveos-airplay-gst.bin
export GST_REGISTRY_FORK=no
run(){ "$R/lib/ld-linux-armhf.so.3" --library-path "$R/lib:/usr/lib:/lib" "$R/$@"; }
bridge_program="$R/airplay-hw-bridge"
uxplay_program="$R/uxplay"
if [ -r "$B/airplay-hw-bridge-fixed" ];then
 if ! (cd "$B";sha256sum -c helpers.sha256) > /tmp/doveos-airplay-helpers.log 2>&1;then
  echo 'AirPlay application file verification failed. Repair or reinstall the application before retrying.';exit 3
 fi
 cp "$B/airplay-hw-bridge-fixed" /tmp/doveos-airplay-bridge.new
 cmp "$B/airplay-hw-bridge-fixed" /tmp/doveos-airplay-bridge.new
 chmod 700 /tmp/doveos-airplay-bridge.new
 mv /tmp/doveos-airplay-bridge.new /tmp/doveos-airplay-bridge
 bridge_program=/tmp/doveos-airplay-bridge
fi
if [ -r "$B/uxplay-clocked" ];then
 if ! (cd "$B";sha256sum -c helpers.sha256) > /tmp/doveos-airplay-helpers.log 2>&1;then
  echo 'AirPlay application file verification failed. Repair or reinstall the application before retrying.';exit 3
 fi
 cp "$B/uxplay-clocked" /tmp/doveos-airplay-uxplay.new
 cmp "$B/uxplay-clocked" /tmp/doveos-airplay-uxplay.new
 chmod 700 /tmp/doveos-airplay-uxplay.new
 mv /tmp/doveos-airplay-uxplay.new /tmp/doveos-airplay-uxplay
 uxplay_program=/tmp/doveos-airplay-uxplay
 # Match audio and video presentation clocks. The bridge preserves references
 # and refuses incompatible slices rather than silently corrupting the picture.
 export DOVE_AIRPLAY_CLOCKED_RELAY=1 DOVE_AIRPLAY_LOW_DELAY_TRIAL=1
fi
if [ "${1:-start}" = check ];then
 "$R/lib/ld-linux-armhf.so.3" --library-path "$R/lib:/usr/lib:/lib" "$bridge_program" --check
 "$R/lib/ld-linux-armhf.so.3" --library-path "$R/lib:/usr/lib:/lib" "$uxplay_program" -v
 exit 0
fi
mkdir "$LOCK" 2>/dev/null || { echo 'AirPlay is already running. Stop it first.';exit 4; }
echo "$$" > "$LOCK/owner"
bridge= receiver= quit=
cleanup(){
 trap - EXIT INT TERM HUP
 for pid in "$receiver" "$quit";do [ -z "$pid" ] || kill "$pid" 2>/dev/null || :;done
 [ -z "$bridge" ] || kill "$bridge" 2>/dev/null || :
 # Allow the decoder to stop VOUT and release its own display ownership.
 for n in $(seq 1 10);do [ -n "$bridge" ] && kill -0 "$bridge" 2>/dev/null || break;sleep 1;done
 for pid in "$receiver" "$quit" "$bridge";do [ -z "$pid" ] || { kill -9 "$pid" 2>/dev/null || :;wait "$pid" 2>/dev/null || :; };done
 if [ "$(cat /tmp/doveos-direct-display 2>/dev/null || :)" = "$bridge" ];then rm -f /tmp/doveos-direct-display;fi
 rm -f "$LOCK/owner" "$STOP";rmdir "$LOCK" 2>/dev/null || :
 echo 'AirPlay stopped. DoveOS display restored.'
}
trap cleanup EXIT INT TERM HUP
rm -f "$STOP"
test ! -e /tmp/doveos-direct-display || { echo 'Another stream owns the display.';exit 4; }
audio=${DOVE_AIRPLAY_AUDIO:-audio/x-raw,format=S16LE,rate=48000,channels=2 ! alsasink device=plughw:RTKAlsaCard,0 buffer-time=100000 latency-time=10000}
# An already-open preview UI may still supply the old USB endpoint. Normalize
# that specific endpoint to the same native speaker route as DoveOS sound tests.
audio=$(printf '%s\n' "$audio" | sed 's/device=plughw:1,0/device=plughw:RTKAlsaCard,0/g')
duration=${DOVE_AIRPLAY_DURATION:-1800}
case "$duration" in ''|*[!0-9]*) exit 2;;esac
[ "$duration" -ge 5 ] && [ "$duration" -le 1800 ] || exit 2
DOVE_AIRPLAY_CLAIM=1 "$R/lib/ld-linux-armhf.so.3" --library-path "$R/lib:/usr/lib:/lib" "$bridge_program" "$duration" > /tmp/doveos-airplay-video.log 2>&1 &
bridge=$!
"$R/lib/ld-linux-armhf.so.3" --library-path "$R/lib:/usr/lib:/lib" "$uxplay_program" \
 -n 'DoveAir' -nh -s '1280x720@30' -fps 30 \
 -as "$audio" -vrtp 'pt=96 ! udpsink host=127.0.0.1 port=17004' -p 17100 \
 > /tmp/doveos-airplay-receiver.log 2>&1 &
receiver=$!
# This helper observes the keyboard shortcut, with no exclusive input grab.
if [ -r "$B/airplay-quit" ];then
 if ! (cd "$B";sha256sum -c helpers.sha256) > /tmp/doveos-airplay-helpers.log 2>&1;then
  echo 'AirPlay application file verification failed. Repair or reinstall the application before retrying.';exit 3
 fi
 cp "$B/airplay-quit" /tmp/doveos-airplay-quit.new
 cmp "$B/airplay-quit" /tmp/doveos-airplay-quit.new
 chmod 700 /tmp/doveos-airplay-quit.new;mv /tmp/doveos-airplay-quit.new /tmp/doveos-airplay-quit
 "$M/lib/ld-linux-armhf.so.3" --library-path "$M/usr/lib/arm-linux-gnueabihf:$M/lib/arm-linux-gnueabihf" /tmp/doveos-airplay-quit --stop-file "$STOP" > /tmp/doveos-airplay-quit.log 2>&1 &
 quit=$!
fi
echo 'Preparing AirPlay receiver...'
ready=0
for n in $(seq 1 25);do
 kill -0 "$receiver" 2>/dev/null && kill -0 "$bridge" 2>/dev/null || break
 if grep -q 'raop_started port=' /tmp/doveos-airplay-receiver.log;then ready=1;break;fi
 sleep 1
done
if [ "$ready" != 1 ];then
 echo 'Receiver did not become ready. See /tmp/doveos-airplay-receiver.log.'
 tail -15 /tmp/doveos-airplay-receiver.log
 exit 5
fi
echo 'Listening as DoveAir. Use Screen Mirroring on the same network.'
echo 'Ctrl+Alt+Shift+Q returns to DoveOS. Preview stops after 30 minutes.'
previous_geometry=
while [ ! -e "$STOP" ] && kill -0 "$receiver" 2>/dev/null && kill -0 "$bridge" 2>/dev/null;do
 geometry=$(tail -400 /tmp/doveos-airplay-video.log | sed -n 's/.*AIRPLAY_DECODER_STARTED.*visible=//p' | tail -1)
 if [ -n "$geometry" ] && [ "$geometry" != "$previous_geometry" ];then
  echo "Mirroring $geometry. Sound uses the selected speaker setting."
  previous_geometry=$geometry
 fi
 sleep 1
done
if [ ! -e "$STOP" ];then
 if ! kill -0 "$bridge" 2>/dev/null;then
  result=0;wait "$bridge" || result=$?
  if [ "$result" != 0 ];then
   echo 'Video pipeline stopped unexpectedly. Decoder details:'
   tail -12 /tmp/doveos-airplay-video.log
   exit 6
  fi
 elif ! kill -0 "$receiver" 2>/dev/null;then
  echo 'AirPlay connection service stopped unexpectedly. Connection details:'
  tail -12 /tmp/doveos-airplay-receiver.log
  exit 7
 fi
fi
