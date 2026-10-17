#!/bin/sh
# Called once after 15 healthy desktop supervision ticks. A failed splash
# still gets its protected HP fallback boot; only that successful later
# desktop may re-enable the following boot's splash.
set -eu
B=/usr/local/etc/doveos-foreground-trial-20261003
U=/media/usb0/DoveOS-v2/boot-splash
case "${1:---rearm}" in --check|--rearm) action=${1:---rearm};; *) exit 2;; esac
[ -f "$B/in-progress" ] || { echo SPLASH_ALREADY_ARMED;exit 0; }
[ -f "$B/enabled" ] && [ ! -e "$U/disabled" ]
[ ! -e /tmp/doveos-v2-stop ]
[ -e /tmp/doveos-splash-handoff ] && [ -S /tmp/.X11-unix/X7 ]
pidof jwm >/dev/null && pidof wfd_dfb >/dev/null
test "$(sha256sum /usr/local/bin/IMS_Modules/wfd_dfb | cut -d ' ' -f 1)" = cb3b26154ce5f07113c614a2d7f03c59ac113333340f96966c31f9e7cdb295c2
(cd "$B" && sha256sum -c internal.sha256)
(cd "$U" && sha256sum -c trial.sha256)
old=$(cat "$B/in-progress")
current=$(cat /proc/sys/kernel/random/boot_id)
# Never clear an attempt that is still owned by this boot's splash helper.
if [ "$old" = "$current" ];then echo SPLASH_CURRENT_ATTEMPT_RETAINED;exit 0;fi
[ "${#old}" -eq 36 ]
case "$old" in *[!0-9a-f-]*) exit 3;; esac
[ "$action" != --check ] || { echo STALE_SPLASH_GUARD_SAFE_TO_REARM;exit 0; }
printf 'Interrupted splash boot: %s\nRecovered desktop boot: %s\n' "$old" "$current" > "$U/last-interrupted-boot.txt.new"
mv "$U/last-interrupted-boot.txt.new" "$U/last-interrupted-boot.txt"
sync
# Fail closed if another writer changed the guard after validation.
[ "$(cat "$B/in-progress")" = "$old" ]
rm "$B/in-progress"
sync
echo SPLASH_REARMED_AFTER_HEALTHY_FALLBACK
