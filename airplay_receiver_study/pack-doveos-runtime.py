"""Build a narrow, file-backed ARM AirPlay runtime (video and AAC/ALAC audio)."""
from pathlib import Path
import hashlib, re, shutil, subprocess
base=Path(__file__).resolve().parent
runtime=Path('/home/hakami55/doveos-airplay-runtime-20261005')
runtime.mkdir(exist_ok=True)
(runtime/'lib').mkdir(exist_ok=True)
(runtime/'plugins').mkdir(exist_ok=True)
executables={'uxplay':base/'build-armhf/uxplay','airplay-hw-bridge':base/'airplay-hw-bridge-arm'}
for name,path in executables.items():
    shutil.copyfile(path,runtime/name);(runtime/name).chmod(0o755)
names=['coreelements','app','videoparsersbad','rtp','udp','libav','audioconvert',
       'audioresample','volume','level','playback','autodetect','typefindfunctions',
       'audioparsers','audiotestsrc']
plugins=[Path('/usr/lib/arm-linux-gnueabihf/gstreamer-1.0')/('libgst'+name+'.so') for name in names]
plugins.append(Path('/home/hakami55/doveos-airplay-alsa-20261005/root/usr/lib/arm-linux-gnueabihf/gstreamer-1.0/libgstalsa.so'))
for path in plugins: shutil.copyfile(path,runtime/'plugins'/path.name)
pending=list(executables.values())+plugins
roots=[Path('/lib/arm-linux-gnueabihf'),Path('/usr/lib/arm-linux-gnueabihf')]
known=set()
while pending:
    path=pending.pop()
    output=subprocess.check_output(['readelf','-d',str(path)],text=True)
    for name in re.findall(r'Shared library: \[([^]]+)\]',output):
        if name in known: continue
        source=next((root/name for root in roots if (root/name).exists()),None)
        if source is None: raise RuntimeError('Missing dependency '+name)
        known.add(name);shutil.copyfile(source,runtime/'lib'/name);pending.append(source)
shutil.copyfile('/lib/ld-linux-armhf.so.3',runtime/'lib/ld-linux-armhf.so.3')
(runtime/'lib/ld-linux-armhf.so.3').chmod(0o755)
shutil.copyfile(base/'UxPlay-current/LICENSE',runtime/'LICENSE-UxPlay')
files=sorted(path for path in runtime.rglob('*') if path.is_file() and path.name!='files.sha256')
(runtime/'files.sha256').write_text(''.join(hashlib.sha256(path.read_bytes()).hexdigest()+'  '+path.relative_to(runtime).as_posix()+'\n' for path in files))
print('AIRPLAY_RUNTIME_FILES',len(files),'BYTES',sum(path.stat().st_size for path in files))
