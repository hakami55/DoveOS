#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
sh "$HERE/../sdk/build-app.sh" "$HERE/prompt.c" "$HERE/doveos-dos-prompt"
