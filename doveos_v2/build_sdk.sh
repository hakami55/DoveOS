#!/bin/sh
# Build SDK demo artifacts on the existing PC/WSL toolchain, not on the dock.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
sh "$HERE/sdk/build-app.sh" "$HERE/sdk/examples/controls.c" "$HERE/artifacts/sdk-controls-standalone"
sh "$HERE/sdk/build-app.sh" "$HERE/sdk/examples/workbench.c" "$HERE/artifacts/doveos-sdk-v2"
# Keep old launch paths compatible; both names open the combined demo.
cp "$HERE/artifacts/doveos-sdk-v2" "$HERE/artifacts/doveos-sdk-demo"
