#!/bin/sh
# Player-local output selection. The stock audio configuration is unchanged.
set -eu
A=/tmp/doveos-bluealsa
B=/tmp/doveos-bluetooth
M=/tmp/doveos-v2-root
U=/media/usb0/DrMoSa/Bluetooth
kind=${1:-}; shift
case "$kind" in raw) [ "$#" = 2 ] || exit 2;; host) [ "$#" -ge 1 ] || exit 2;; *) exit 2;; esac
bluetooth=0
address=$(cat "$U/audio-device.txt" 2>/dev/null || :)
if printf '%s\n' "$address" | grep -Eq '^[[:xdigit:]]{2}(:[[:xdigit:]]{2}){5}$' &&
   [ -x "$A/bluealsa-cli-debug" ] && [ -r "$A/lib/libasound_module_pcm_bluealsa.so" ]; then
    mac=$(printf '%s' "$address" | tr ':' '_' | tr 'a-f' 'A-F')
    L=$A/lib:$M/usr/lib/arm-linux-gnueabihf:$M/lib/arm-linux-gnueabihf:$B/lib
    if "$A/bounded-run" 3 "$A/unblock-exec" "$B/lib/ld-linux-armhf.so.3" --library-path "$L" \
       "$A/bluealsa-cli-debug" info "/org/bluealsa/hci0/dev_${mac}/a2dpsrc/sink" >/dev/null 2>&1; then
        bluetooth=1
    fi
fi
if [ "$bluetooth" = 1 ]; then
    config=$(mktemp "$A/player-route.XXXXXX")
    # This predefined PCM includes a plug converter for the source sample rate.
    cat >"$config" <<CONFIG
</tmp/doveos-bluealsa/share/alsa/alsa.conf>
</tmp/doveos-bluealsa/20-bluealsa.conf>
pcm_type.bluealsa { lib /tmp/doveos-bluealsa/lib/libasound_module_pcm_bluealsa.so }
ctl_type.bluealsa { lib /tmp/doveos-bluealsa/lib/libasound_module_ctl_bluealsa.so }
CONFIG
    # One stable config is sufficient: the device itself is a PCM argument.
    mv "$config" "$A/player-route.conf"
    export ALSA_CONFIG_DIR="$A/share/alsa" ALSA_CONFIG_PATH="$A/player-route.conf" ALSA_PLUGIN_DIR="$A/lib"
    export DOVE_AUDIO_ALSA_LIBRARY="$A/lib/libasound.so.2"
    export DOVE_AUDIO_PCM="bluealsa:DEV=$address,PROFILE=a2dp"
    echo "AUDIO_ROUTE=bluetooth device=$address" >&2
    if [ "$kind" = raw ]; then
        exec "$M/lib/ld-linux-armhf.so.3" --library-path "$L" "$A/aplay" \
            -q -N -B 100000 -F 25000 -D "$DOVE_AUDIO_PCM" -t raw -f S16_LE -r "$1" -c "$2"
    fi
    exec "$M/lib/ld-linux-armhf.so.3" --library-path "$L" "$@"
fi
unset DOVE_AUDIO_ALSA_LIBRARY DOVE_AUDIO_PCM
echo 'AUDIO_ROUTE=built-in (automatic 3.5 mm jack routing)' >&2
if [ "$kind" = raw ]; then
    exec /usr/bin/aplay -q -N -B 100000 -F 25000 -D plughw:RTKAlsaCard,0 -t raw -f S16_LE -r "$1" -c "$2"
fi
exec "$M/lib/ld-linux-armhf.so.3" --library-path "$M/usr/lib/arm-linux-gnueabihf:$M/lib/arm-linux-gnueabihf:/usr/lib:/lib" "$@"
