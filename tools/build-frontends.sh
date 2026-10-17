#!/bin/sh
# Build the receiver UI and small desktop tools from this repository.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out=${1:-$repo/build/frontends}
mkdir -p "$out"
sdk=$repo/doveos_v2/sdk/build-app.sh
sh "$sdk" "$repo/doveos_v2/settings_gtk.c" "$out/doveos-settings-ui"
for app in calculator notepad minesweeper;do
 sh "$sdk" "$repo/doveos_v2/classic_apps/$app.c" "$out/doveos-$app" -lm
done
sh "$sdk" "$repo/doveos_v2/dovedex/dovedex.c" "$out/dovedex" -lpthread -ldl
sh "$sdk" "$repo/airplay_receiver_study/airplay-ui.c" "$out/doveair-ui"
for app in controls workbench;do
 sh "$sdk" "$repo/doveos_v2/sdk/examples/$app.c" "$out/doveos-sdk-$app" -lm
done
