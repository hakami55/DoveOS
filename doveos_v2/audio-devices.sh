#!/bin/sh
# Validated ALSA device preferences; only the DoveOS RAM home is configured.
AUDIO_PROFILE=/media/usb0/DoveOS-v2/home/audio-devices.txt
audio_valid(){
 case "$1" in *,*) ;; *) return 1;;esac
 card=${1%,*};dev=${1##*,}
 case "$card" in ''|*[!A-Za-z0-9_-]*) return 1;;esac
 case "$dev" in ''|*[!0-9]*) return 1;;esac
 [ "$dev" -le 31 ] 2>/dev/null
}
audio_available(){
 audio_valid "$1" || return 1
 case "$2" in p|c) ;; *) return 1;;esac
 # The vendor RTK capture endpoint is not a verified physical microphone.
 [ "$2" != c ] || [ "$card" != RTKAlsaCard ] || return 1
 for audio_path in /proc/asound/card[0-9]*;do
  [ "$(cat "$audio_path/id" 2>/dev/null)" = "$card" ] || continue
  [ -r "$audio_path/pcm${dev}$2/info" ] && return 0
 done
 return 1
}
