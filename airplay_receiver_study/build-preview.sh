#!/bin/sh
set -eu
B=/mnt/c/vscode/HPlapdock
python3 - <<'PY'
from pathlib import Path
b=Path('/mnt/c/vscode/HPlapdock')
s=(b/'doveos_v2/dovedex/dovedex-quit.c').read_text()
s=s.replace('/tmp/dovedex-runtime-v1/session.stop','/tmp/doveos-airplay.stop')
s=s.replace('/tmp/drmosa-moonlight.stop','/tmp/doveos-airplay.stop')
(b/'airplay_receiver_study/airplay-quit.c').write_text(s)
PY
arm-linux-gnueabihf-gcc -O2 -Wall -Wextra "$B/airplay_receiver_study/airplay-quit.c" -o "$B/airplay_receiver_study/airplay-quit"
sh "$B/doveos_v2/sdk/build-app.sh" "$B/airplay_receiver_study/airplay-ui.c" "$B/airplay_receiver_study/airplay-ui"
/opt/lapdock-focal-cross/usr/bin/arm-linux-gnueabihf-gcc --sysroot=/opt/lapdock-focal-cross -O2 -Wall -Wextra "$B/airplay_receiver_study/receiver-run.c" -o "$B/airplay_receiver_study/receiver-run"
