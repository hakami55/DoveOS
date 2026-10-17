#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
sh "$HERE/../sdk/build-app.sh" "$HERE/minesweeper.c" "$HERE/doveos-minesweeper"
sh "$HERE/../sdk/build-app.sh" "$HERE/calculator.c" "$HERE/doveos-calculator" -lm
sh "$HERE/../sdk/build-app.sh" "$HERE/notepad.c" "$HERE/doveos-notepad"
(cd "$HERE"; sha256sum doveos-minesweeper doveos-calculator doveos-notepad launch.sh install.sh \
 Minesweeper.desktop Calculator.desktop Notepad.desktop README.txt Chicago95-GPL-3.0.txt \
 Chicago95-CREDITS.txt icons/*.png > files.sha256)
