"""Package a built ARM app with a checked RAM launcher and desktop shortcut."""
from pathlib import Path
import argparse,hashlib,json,re,tarfile

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('manifest',type=Path);p.add_argument('binary',type=Path);p.add_argument('destination',type=Path)
    a=p.parse_args();info=json.loads(a.manifest.read_text(encoding='utf-8'))
    app_id=info['id'];name=info['name']
    if not re.fullmatch(r'[a-z][a-z0-9-]{0,47}',app_id):p.error('invalid app id')
    if not isinstance(name,str) or not name or any(ord(c)<32 for c in name) or '\\' in name:p.error('invalid display name')
    data=a.binary.read_bytes()
    if len(data)<20 or data[:5]!=b'\x7fELF\x01' or data[5]!=1 or int.from_bytes(data[18:20],'little')!=40:p.error('binary must be a little-endian ARM ELF32 executable')
    target=a.destination.resolve()
    if target.exists():p.error('destination exists')
    archive=target.with_name(target.name+'.tar.gz')
    if archive.exists():p.error('archive already exists')
    target.mkdir(parents=True);digest=hashlib.sha256(data).hexdigest()
    (target/app_id).write_bytes(data)
    launcher=f'''#!/bin/sh
set -eu
B=/media/usb0/DoveOS-v2/Apps/{app_id}
R=/tmp/doveos-apps/{app_id}
mkdir -p /tmp/doveos-apps
test "$(sha256sum "$B/{app_id}" | cut -d ' ' -f1)" = {digest}
cp "$B/{app_id}" "$R.new.$$"
test "$(sha256sum "$R.new.$$" | cut -d ' ' -f1)" = {digest}
chmod 700 "$R.new.$$"
mv "$R.new.$$" "$R"
export DISPLAY="${{DISPLAY:-:7}}" HOME="${{HOME:-/root}}"
export GTK_PATH="${{GTK_PATH:-/tmp/doveos-theme/gtk-modules}}"
export GDK_PIXBUF_MODULE_FILE="${{GDK_PIXBUF_MODULE_FILE:-/tmp/doveos-v2-pixbuf.cache}}"
exec "$R" "$@"
'''
    (target/'launch.sh').write_bytes(launcher.encode())
    desktop=f'[Desktop Entry]\nType=Application\nName={name}\nExec=/bin/sh /media/usb0/DoveOS-v2/Apps/{app_id}/launch.sh\nIcon=applications-utilities\nTerminal=false\n'
    (target/(app_id+'.desktop')).write_bytes(desktop.encode('utf-8'))
    (target/'app.json').write_text(json.dumps(info,indent=2)+'\n',encoding='utf-8')
    (target/'INSTALL.txt').write_text(f'Copy this folder to /media/usb0/DoveOS-v2/Apps/{app_id}.\nCopy {app_id}.desktop into your persistent home .local/share/applications folder.\nRun the launcher with /bin/sh from the DoveOS runtime. The USB stays noexec.\n',encoding='utf-8')
    files=sorted(x for x in target.iterdir() if x.is_file())
    (target/'files.sha256').write_bytes(''.join(hashlib.sha256(x.read_bytes()).hexdigest()+'  '+x.name+'\n' for x in files).encode('ascii'))
    with tarfile.open(archive,'w:gz') as tar:
        for file in sorted(target.iterdir()):tar.add(file,arcname=app_id+'/'+file.name)
    print(archive)
if __name__=='__main__':main()
