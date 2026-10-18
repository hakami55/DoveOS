#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
convert -background none "$HERE/power.svg" "$HERE/power-48.png"
convert "$HERE/power-48.png" -resize 24x24 "$HERE/power-24.png"
sh "$HERE/../sdk/build-app.sh" "$HERE/../settings_gtk.c" "$HERE/doveos-settings-ui"
sh "$HERE/../sdk/build-app.sh" "$HERE/../doveplayer/ui.c" "$HERE/../doveplayer/doveplayer-ui"
sh "$HERE/../sdk/build-app.sh" "$HERE/../controllers/controller_tester.c" "$HERE/../controllers/doveos-controller-tester"
sh "$HERE/../local_transfer/build.sh"
(cd "$HERE";sha256sum doveos-settings-ui install.sh power-24.png power-48.png > files.sha256)
