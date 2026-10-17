#!/bin/sh
# Run existing hardware-facing apps while the X11 display bridge is stopped.
set -u
ROOT=/media/usb0/DoveOS-v2
RUNTIME=/tmp/drmosa-usb-menu-runtime
LOADER=$RUNTIME/lib/ld-linux-armhf.so.3
LIB=$RUNTIME/lib
LOG=$ROOT/app-handoff.log
case "${1:-}" in
 hdmi)
  test -r "$RUNTIME/hdmi-viewer" || exit 2
  echo HDMI_START >> "$LOG"
  "$LOADER" --library-path "$LIB" "$RUNTIME/hdmi-viewer" >> "$LOG" 2>&1
  echo "HDMI_EXIT=$?" >> "$LOG"
  ;;
 media)
  IFS= read -r selected < /tmp/doveos-v2-media-path || exit 2
  rm -f /tmp/doveos-v2-media-path
  case "$selected" in /media/usb0/*.mp4|/media/usb0/*.wav|/media/usb0/*.mp3|/media/usb0/*.mkv) ;;
   *) echo MEDIA_PATH_REJECTED >> "$LOG";exit 3;;esac
  test -f "$selected" || exit 3
  test -r "$RUNTIME/media-player" || exit 2
  echo MEDIA_START >> "$LOG"
  "$LOADER" --library-path "$LIB" "$RUNTIME/media-player" "$selected" >> "$LOG" 2>&1
  echo "MEDIA_EXIT=$?" >> "$LOG"
  ;;
 games)
  test -r /tmp/drmosa-usb-menu || exit 2
  echo GAMES_START >> "$LOG"
  while :;do
   "$LOADER" --library-path "$LIB" /tmp/drmosa-usb-menu /media/usb0 0 --rom-launcher >> "$LOG" 2>&1
   code=$?
   test "$code" = 14 || break
   IFS= read -r selected < /tmp/drmosa-rom-selected || break
   rm -f /tmp/drmosa-rom-selected
   case "$selected" in /media/usb0/DrMoSa/Games/ROMs/NES/*.nes) ;;
    *) echo GAME_PATH_REJECTED >> "$LOG";continue;;esac
   test -f "$selected" || continue
   "$LOADER" --library-path "$LIB" "$RUNTIME/libretro-dock" "$RUNTIME/fceumm_libretro.so" "$selected" >> "$LOG" 2>&1
   echo "GAME_EXIT=$?" >> "$LOG"
  done
  echo GAMES_CLOSED >> "$LOG"
  ;;
 *) exit 2;;
esac
exit 0
