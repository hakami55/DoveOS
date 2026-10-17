import sys,urllib.request,hashlib,subprocess,json
from pathlib import Path
sys.path.insert(0,'/mnt/c/vscode/HPlapdock/doveos_v2')
import stage_optional_apps as stage
names=['libavformat-dev','libavcodec-dev','libavutil-dev','libswresample-dev']
pkgs=stage.resolve(names)
q=Path('/home/hakami55/doveplayer-sdk');q.mkdir(exist_ok=True)
manifest={}
for name in names:
    v=pkgs[name];data=urllib.request.urlopen(v.uri.replace('http:','https:'),timeout=40).read()
    assert hashlib.sha256(data).hexdigest()==v.sha256
    f=q/v.filename.split('/')[-1];f.write_bytes(data)
    subprocess.run(['dpkg-deb','-x',str(f),str(q/'root')],check=True)
    manifest[name]={'version':v.version,'url':v.uri,'sha256':v.sha256}
(q/'headers.json').write_text(json.dumps(manifest,indent=2))
print('HEADERS_READY',flush=True)
