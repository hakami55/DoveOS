#!/bin/sh
# Runs inside the current desktop chroot. All executable files live in RAM.
set -eu
B=${DOVEDEX_BASE:-/media/usb0/DoveOS-v2/dovedex}
R=/tmp/dovedex-runtime-v1
action=${1:-check};mode=${2:-usb};address=${3:-}
case "$mode" in usb) adbname=adb-usb;port=5040;; wifi) adbname=adb-wifi;port=5039;; *) exit 2;;esac
mkdir -p "$R" "$B/diagnostics"
expected=629d68e1ab6e2d38d5fd1e56555414d009189c9c3db6222e364e5d5cb4740ca5
actual=$(sha256sum "$B/runtime.tar.gz");test "${actual%% *}" = "$expected" || { echo 'Runtime verification failed.';exit 2; }
if [ "$(cat "$R/.verified" 2>/dev/null || :)" != "$expected" ];then
 gunzip -c "$B/runtime.tar.gz" | tar -xf - -C "$R"
 (cd "$R";sha256sum -c contents.sha256 >/dev/null)
 chmod 700 "$R/adb-usb" "$R/adb-wifi" "$R/dovedex-run" "$R/dovedex-viewer" "$R/dovedex-rtk-host"
 printf '%s\n' "$expected" > "$R/.verified"
fi
desktop_home=${HOME:-/root}
adb_home="$desktop_home/.config/dovedex/adb-home"
mkdir -p "$adb_home";chmod 700 "$adb_home"
adb_lib="$R/lib/android:$R/lib:/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf"
adb(){ HOME="$adb_home" LD_LIBRARY_PATH="$adb_lib" ADB_LIBUSB=1 "$R/dovedex-run" 8 "$R/$adbname" -P "$port" "$@"; }
ensure_server(){
server_mode=$1;server_name=$2;server_port=$3
pidfile="$R/server-$server_mode.pid"
owned=0
if [ -r "$pidfile" ];then
 pid=$(cat "$pidfile")
 case "$pid" in ''|*[!0-9]*) exit 2;;esac
 if [ -r "/proc/$pid/cmdline" ];then
  cmd=$(tr '\000' ' ' < "/proc/$pid/cmdline")
  case "$cmd" in *"$R/dovedex-run 1800 $R/$server_name -P $server_port nodaemon server"*) owned=1;; *) echo 'Server owner mismatch. Refusing to signal another process.';exit 2;;esac
 fi
fi
if [ "$owned" = 0 ];then
 HOME="$adb_home" LD_LIBRARY_PATH="$adb_lib" ADB_LIBUSB=1 "$R/dovedex-run" 1800 "$R/$server_name" -P "$server_port" nodaemon server >"$B/diagnostics/adb-$server_mode.log" 2>&1 </dev/null &
 pid=$!;echo "$pid" > "$pidfile";sleep 2
 kill -0 "$pid" || { echo 'ADB server startup failed. See diagnostics.';exit 2; }
fi
}
ensure_server "$mode" "$adbname" "$port"
wifi_adb(){ HOME="$adb_home" LD_LIBRARY_PATH="$adb_lib" "$R/dovedex-run" 8 "$R/adb-wifi" -P 5039 "$@"; }
if [ "$mode" = wifi ];then
 # Keep shell arguments structured; accept an IPv4 address and numeric port only.
 case "$address" in ''|*[!0-9.:]*) echo 'Enter an IPv4 address and connection port, for example 192.0.2.10:5555.';exit 2;;esac
 host=${address%:*};number=${address##*:}
 printf '%s\n' "$host" | awk -F. 'NF != 4 {exit 1} {for(i=1;i<=4;i++) if($i !~ /^[0-9]+$/ || length($i)>3 || $i+0>255) exit 1}' || { echo 'A valid IPv4 address is required.';exit 2; }
 case "$number" in ''|*[!0-9]*) exit 2;;esac
 [ "$number" -gt 0 ] && [ "$number" -lt 65536 ] || exit 2
 adb connect "$address"
 serial=$address
else
 devices=$(adb devices -l)
 printf '%s\n' "$devices"
 serial=$(printf '%s\n' "$devices" | awk '$2 == "device" || $2 == "unauthorized" || $2 == "offline" {print $1}')
 count=$(printf '%s\n' "$serial" | awk 'NF {n++} END {print n+0}')
 [ "$count" = 1 ] || { echo 'Connect one USB debugging device. Multiple-device selection is not implemented yet.';exit 2; }
fi
state=$(adb -s "$serial" get-state 2>/dev/null || :)
if [ "$state" != device ] && [ "$mode" = wifi ];then
 # TCP connect may finish before the authenticated transport goes online.
 # The owned runner bounds this wait to eight seconds.
 adb -s "$serial" wait-for-device >/dev/null 2>&1 || :
 state=$(adb -s "$serial" get-state 2>/dev/null || :)
fi
if [ "$state" != device ];then
 echo 'Not authorized yet. Unlock the phone and accept Allow USB debugging, then click Check again.'
 exit 3
fi
model=$(adb -s "$serial" shell getprop ro.product.model | tr -d '\r')
sdk=$(adb -s "$serial" shell getprop ro.build.version.sdk | tr -d '\r')
echo "Connected and authorized: $model (Android API $sdk)."
if [ "$action" = prepare-wifi ];then
 [ "$mode" = usb ] || { echo 'Connect an authorized USB device for Wi-Fi setup.';exit 2; }
 dock_cidr=$(ip -4 addr show wlan0 | awk '/inet / {print $2;exit}')
 gateway=$(ip route | awk '$1=="default"&&$2=="via" {for(i=4;i<NF;i++)if($i=="dev"&&$(i+1)=="wlan0"){print $3;exit}}')
 phone_ips=$(adb -s "$serial" shell ip -4 addr show | tr -d '\r' | awk '/inet / {split($2,a,"/");print a[1]}')
 phone_ip=$(printf '%s\n' "$phone_ips" | awk -v cidr="$dock_cidr" -v gateway="$gateway" -f "$B/wifi-address.awk") || {
  echo 'No unique device address on the dock Wi-Fi network. Connect DoveOS to the device hotspot, or put both on the same Wi-Fi, then repeat setup.';exit 4;
 }
 usb_identity=$(adb -s "$serial" shell getprop ro.serialno | tr -d '\r')
 [ -n "$usb_identity" ] || { echo 'Device identity unavailable; Wi-Fi setup was not saved.';exit 4; }
 if [ "$phone_ip" = "$gateway" ];then echo "Device hotspot detected: $phone_ip.";else echo "Device address on the dock Wi-Fi network: $phone_ip.";fi
 current_port=$(adb -s "$serial" shell getprop service.adb.tcp.port | tr -d '\r')
 if [ "$current_port" != 5555 ];then adb -s "$serial" tcpip 5555;fi
 ensure_server wifi adb-wifi 5039
 wifi_adb connect "$phone_ip:5555"
 wifi_state=$(wifi_adb -s "$phone_ip:5555" get-state 2>/dev/null || :)
 if [ "$wifi_state" != device ];then
  wifi_adb -s "$phone_ip:5555" wait-for-device >/dev/null 2>&1 || :
  wifi_state=$(wifi_adb -s "$phone_ip:5555" get-state 2>/dev/null || :)
 fi
 [ "$wifi_state" = device ] || { echo 'Wi-Fi ADB is not authorized or reachable. Keep USB connected, accept any debugging prompt, then repeat setup.';exit 4; }
 wifi_identity=$(wifi_adb -s "$phone_ip:5555" shell getprop ro.serialno | tr -d '\r')
 if [ "$wifi_identity" != "$usb_identity" ];then
  wifi_adb disconnect "$phone_ip:5555" >/dev/null 2>&1 || :
  echo 'Wi-Fi device identity differs from the authorized USB device. Address was not saved.';exit 4;
 fi
 echo 'Wi-Fi connection authorized; identity matches the USB device.'
 echo "DOVEDEX_WIFI_ENDPOINT=$phone_ip:5555"
 echo 'Wi-Fi address verified. Check Wi-Fi before removing only the phone USB cable. After a phone restart or network change, repeat USB setup.'
 exit 0
fi
if [ "$action" = check ];then
 if [ "$mode" = wifi ];then echo "Verified Wi-Fi connection: $address";fi
 echo 'USB/Wi-Fi ADB is ready. Click Open USB / Wi-Fi to start the desktop.'
 exit 0
fi
[ "$action" = stream ] || exit 2
provider=virtual
if [ "$model:$sdk" = SM-N960F:29 ];then
 dex_state=$(adb -s "$serial" shell dumpsys desktopmode | tr -d '\r')
 printf '%s\n' "$dex_state" | grep 'mState=.*SemDesktopModeState(ENABLED, STATE_UNDEFINED, DISPLAY_TYPE_DUAL)' >/dev/null || {
  echo 'Android device DeX is not active. This provider needs the separate one-time Android device preparation; it will not reboot your phone automatically.';exit 4;
 }
 dex_display=$(printf '%s\n' "$dex_state" | sed -n 's/^ *mDesktopDisplayId=\([0-9][0-9]*\) *$/\1/p')
 case "$dex_display" in ''|0|*[!0-9]*) echo 'Could not identify the active DeX display.';exit 4;;esac
 provider=Android device
 echo "Using existing Samsung DeX display $dex_display. Your phone desktop stays active when the viewer closes."
else
 [ "$sdk" -ge 33 ] || { echo 'This phone needs Android 13 or newer for a virtual desktop, or a prepared Android device with active DeX.';exit 4; }
fi
# Validated profile fields. Never interpolate phone or user text into a shell command.
bitrate=${4:-4};fps=${5:-30};size=${6:-720};codec=${7:-h264};audio=${8:-0}
audio_delay=${9:-0}
case "$audio_delay" in ''|*[!0-9]*) exit 2;;esac
[ "$audio_delay" -le 250 ] || exit 2
case "$bitrate:$fps:$size" in *[!0-9:]*) exit 2;;esac
[ "$bitrate" -ge 1 ] && [ "$bitrate" -le 20 ] && [ "$fps" -ge 15 ] && [ "$fps" -le 60 ] || { echo 'This receiver supports up to 20 Mb/s and 60 FPS.';exit 4; }
case "$size" in 720) display=1280x720/160;max_size=1280;;1080) display=1920x1080/240;max_size=1920;;*) exit 4;;esac
display_option="new_display=$display"
[ "$provider" != Android device ] || display_option="display_id=$dex_display"
test_option=log_level=info
case "${DOVEDEX_TEST_APP:-}" in ''|org.doveos.dovedexmotion) ;;*) exit 4;;esac
case "$codec" in h264|h265) ;;hevc) codec=h265;;*) exit 4;;esac
case "$audio" in 0|1) ;;*) exit 4;;esac
if [ "$audio" = 1 ] && [ "$sdk" -lt 30 ];then
 echo 'This Android version cannot forward sound; continuing with video only.'
 audio=0
fi
sound_option=false
[ "$audio" = 0 ] || sound_option=true
mkdir "$R/session-lock" 2>/dev/null || { echo 'A viewer is already active. Close it with Alt+Q before starting another.';exit 4; }
printf '%s\n' "$$" > "$R/session-lock/owner"
decoder= phone= viewer= quit= pointer= pointer_token= watcher= audio_player= wifi_power=
cleanup(){
 trap - EXIT INT TERM HUP
 [ -z "$watcher" ] || kill "$watcher" 2>/dev/null || :
 if [ -n "$pointer_token" ];then
  adb -s "$serial" shell touch "/data/local/tmp/$pointer_token.stop" >/dev/null 2>&1 || :
  sleep 1
 fi
 # Closing the viewer releases its socket. Let the decoder stop VOUT and free
 # its firmware instance before terminating the broker process group.
 for pid in "$viewer" "$phone" "$quit" "$pointer" "$audio_player";do [ -z "$pid" ] || kill "$pid" 2>/dev/null || :;done
 if [ -n "$decoder" ];then
  closing=0
  while kill -0 "$decoder" 2>/dev/null && ! grep -q '^DIRECT_SESSION_CLOSED$' "$B/diagnostics/decoder.log" && [ "$closing" -lt 12 ];do
   sleep 1;closing=$((closing+1))
  done
  kill "$decoder" 2>/dev/null || :
 fi
 sleep 2
 for pid in "$viewer" "$phone" "$decoder" "$quit" "$pointer" "$watcher" "$audio_player";do [ -z "$pid" ] || { kill -9 "$pid" 2>/dev/null || :;wait "$pid" 2>/dev/null || :; };done
 if [ -n "$wifi_power" ];then
  kill -TERM "$wifi_power" 2>/dev/null || :
  wait "$wifi_power" 2>/dev/null || :
 fi
 rm -f "$R/audio.fifo"
 if [ "$(cat /tmp/doveos-direct-display 2>/dev/null || :)" = "$decoder" ];then rm -f /tmp/doveos-direct-display /tmp/doveos-direct-display.ready;fi
 # Older running desktop bridges paint one stale fb0 snapshot on handoff.
 # Clear it after their first frame; updated bridges no longer create it.
 if [ -x "$R/dovedex-refresh-overlay" ];then
  sleep 1
  "$R/dovedex-refresh-overlay" || :
 fi
 adb -s "$serial" forward --remove tcp:27193 >/dev/null 2>&1 || :
 rm -f "$R/session-lock/owner"
 rmdir "$R/session-lock" 2>/dev/null || :
}
trap cleanup EXIT INT TERM HUP
rm -f "$R/session.stop"
if [ "$mode" = wifi ];then
 # The owned helper restores the original radio setting on stream exit,
 # parent exit or its deadline. No adapter reset or global policy change.
 "$R/wifi-stream-power" --guard "$$" > "$B/diagnostics/wifi-power.log" 2>&1 </dev/null &
 wifi_power=$!
 for n in $(seq 1 40);do
  grep -qE 'PS_READY=1|PS_ALREADY_DISABLED=1' "$B/diagnostics/wifi-power.log" && break
  kill -0 "$wifi_power" 2>/dev/null || break
  sleep 0.1
 done
 if grep -qE 'PS_READY=1|PS_ALREADY_DISABLED=1' "$B/diagnostics/wifi-power.log";then
  echo 'Low-delay Wi-Fi mode active; original power saving returns when streaming ends.'
 else
  echo 'Wi-Fi power-saving control unavailable; streaming continues. See diagnostics/wifi-power.log.'
 fi
fi
"$R/dovedex-run" 1800 "$R/dovedex-quit" --stop-file "$R/session.stop" >"$B/diagnostics/quit.log" 2>&1 </dev/null &
quit=$!
adb -s "$serial" push "$R/scrcpy-server-4.1" /data/local/tmp/dovedex-scrcpy-server-4.1.jar >/dev/null
hash=$(adb -s "$serial" shell sha256sum /data/local/tmp/dovedex-scrcpy-server-4.1.jar)
[ "${hash%% *}" = deacb991ed2509715160ffdc7907e47b4160eb30d1566217e9047fd5b8850cae ] || { echo 'Phone server verification failed.';exit 5; }
adb -s "$serial" forward tcp:27193 localabstract:scrcpy_0d0e0303 >/dev/null
rm -f /tmp/dovedex-rtk.sock /tmp/doveos-direct-display.ready
DOVEDEX_BASE="$B" chroot /proc/1/root /bin/sh "$B/native-decoder.sh" >"$B/diagnostics/decoder.log" 2>&1 </dev/null &
decoder=$!
ticks=0
while [ ! -S /tmp/dovedex-rtk.sock ] && kill -0 "$decoder" 2>/dev/null && [ "$ticks" -lt 12 ];do sleep 1;ticks=$((ticks+1));done
[ -S /tmp/dovedex-rtk.sock ] && kill -0 "$decoder" 2>/dev/null || { cat "$B/diagnostics/decoder.log";exit 5; }
if [ "$audio" = 1 ];then
 rm -f "$R/audio.fifo";mkfifo "$R/audio.fifo";chmod 600 "$R/audio.fifo"
 chmod 700 "$R/dovedex-pcm"
 "$R/dovedex-run" 1800 chroot /proc/1/root "$R/dovedex-pcm" "$R/audio.fifo" >"$B/diagnostics/audio.log" 2>&1 </dev/null &
 audio_player=$!
fi
HOME="$adb_home" LD_LIBRARY_PATH="$adb_lib" ADB_LIBUSB=1 "$R/dovedex-run" 1800 "$R/$adbname" -P "$port" -s "$serial" shell CLASSPATH=/data/local/tmp/dovedex-scrcpy-server-4.1.jar app_process / com.genymobile.scrcpy.Server 4.1 scid=0d0e0303 tunnel_forward=true audio="$sound_option" audio_codec=opus audio_bit_rate=128000 audio_source=output control=true video_codec="$codec" video_bit_rate="${bitrate}000000" max_fps="$fps" "$display_option" max_size="$max_size" cleanup=true "$test_option" >"$B/diagnostics/scrcpy-server.log" 2>&1 </dev/null &
phone=$!
sleep 2
if [ "$provider" = Android device ];then
 adb -s "$serial" push "$R/dovedex-pointer.jar" /data/local/tmp/dovedex-pointer.jar >/dev/null
 localhash=$(sha256sum "$R/dovedex-pointer.jar");remotehash=$(adb -s "$serial" shell sha256sum /data/local/tmp/dovedex-pointer.jar)
 [ "${localhash%% *}" = "${remotehash%% *}" ] || { echo 'Pointer helper transfer verification failed.';exit 5; }
 pointer_token="dovedex-$$"
 adb -s "$serial" shell rm -f "/data/local/tmp/$pointer_token.stop"
 HOME="$adb_home" LD_LIBRARY_PATH="$adb_lib" ADB_LIBUSB=1 "$R/dovedex-run" 1800 "$R/$adbname" -P "$port" -s "$serial" shell CLASSPATH=/data/local/tmp/dovedex-pointer.jar app_process / PointerLease "$dex_display" "$pointer_token" >"$B/diagnostics/pointer.log" 2>&1 </dev/null &
 pointer=$!
fi
echo "Starting direct hardware display: ${size}p, up to ${fps} FPS. Ctrl+Alt+Shift+Q returns to DoveOS. Forward sound: $sound_option."
if [ "$audio" = 1 ];then
 export DOVEDEX_AUDIO_FIFO="$R/audio.fifo"
 export DOVEDEX_AUDIO_DELAY_MS="$audio_delay"
else
 unset DOVEDEX_AUDIO_FIFO
 unset DOVEDEX_AUDIO_DELAY_MS
fi
"$R/dovedex-run" 1800 "$R/dovedex-viewer" 27193 --direct >"$B/diagnostics/viewer.log" 2>&1 &
viewer=$!
if [ "$provider" = Android device ];then
 HOME="$adb_home" LD_LIBRARY_PATH="$adb_lib" ADB_LIBUSB=1 "$R/dovedex-run" 1800 /bin/sh "$B/watch-Android device.sh" "$adbname" "$port" "$serial" "$$" >"$B/diagnostics/dex-exit.log" 2>&1 </dev/null &
 watcher=$!
fi
result=0;wait "$viewer" || result=$?
cat "$B/diagnostics/viewer.log"
echo 'Viewer closed; DoveOS desktop remains running.'
exit "$result"
