#!/bin/sh
# Rebuild all DoveOS-owned GTK frontends against the current shared SDK.
# Stream decoders, drivers and other backend binaries are not built here.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(dirname "$HERE")
build() { sh "$HERE/sdk/build-app.sh" "$@"; }
build "$HERE/settings_gtk.c" "$HERE/artifacts/doveos-settings-ui"
sh "$HERE/explorer/build.sh"
build "$HERE/moonlight-ui/moonlight.c" "$HERE/moonlight-ui/doveos-moonlight-ui"
build "$HERE/dovedex/dovedex.c" "$HERE/dovedex/dovedex"
build "$HERE/dovedex/dovedex-viewer.c" "$HERE/dovedex/dovedex-viewer" -pthread -ldl
build "$HERE/controllers/controller_tester.c" "$HERE/controllers/doveos-controller-tester"
build "$HERE/doveplayer/ui.c" "$HERE/doveplayer/doveplayer-ui"
sh "$HERE/dos_prompt/build.sh"
sh "$HERE/local_transfer/build.sh"
build "$ROOT/airplay_receiver_study/airplay-ui.c" "$ROOT/airplay_receiver_study/airplay-ui"
DOVEOS_UI_ONLY=1 sh "$ROOT/hdmi_input_study/build-userspace-uvc.sh"
build "$ROOT/youtube_cast_study/dock-prototype/youtube-ui.c" "$ROOT/youtube_cast_study/dock-prototype/youtube-ui"
build "$HERE/light-file-picker.c" "$HERE/artifacts/light-file-picker"
sh "$HERE/build_sdk.sh"
sh "$HERE/classic_apps/build.sh"
