"""Generate a side-by-side host launcher using the tested DoveOS app supervisor."""
from pathlib import Path

HERE = Path(__file__).resolve().parent
source = (HERE.parent / 'usb_desktop_poc/payload/autorun/01-drmosa-menu.sh').read_text()
source = source.replace('BIN="$ROOT/DrMoSa/drmosa-usb-menu"', 'BIN="$ROOT/DoveOS-v2/menu-entry.sh"')
source = source.replace('RUN=/tmp/drmosa-usb-menu\n', 'RUN=/tmp/doveos-v2-menu\n')
source = source.replace('LOG="$ROOT/DrMoSa/menu.log"', 'LOG="$ROOT/DoveOS-v2/desktop.log"')
old = '"$RUNTIME/lib/ld-linux-armhf.so.3" --library-path "$RUNTIME/lib" \\\n'
assert source.count(old) >= 2
# Only the two menu calls become shell wrappers; preserve all other app calls.
import re
source, count = re.subn(re.escape(old) + r'\s+("\$RUN")', r'\1', source)
assert count == 2, count
source = source.replace('# Temporary Files/Notes menu. Runs from RAM; does not alter internal storage.',
    '# DoveOS v2 prototype launcher. Desktop loaded from the USB Buildroot image.')
artifacts = HERE / 'artifacts'
artifacts.mkdir(exist_ok=True)
(artifacts / 'start-v2.sh').write_text(source)
(artifacts / 'menu-entry.sh').write_text((HERE / 'menu-entry.sh').read_text())
print('Side-by-side v2 launcher generated; stock autorun remains available')
