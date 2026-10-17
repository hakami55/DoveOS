from pathlib import Path
import hashlib, io, re, tarfile
p=Path(__file__).resolve().parent
root=p.parents[1]
entries=[]
replace={'dovedex-viewer','dovedex-direct-host','contents.sha256'}
with tarfile.open(root/'backups/dovedex-direct-20261003/previous-app/runtime.tar.gz') as source, tarfile.open(p/'runtime.tar.gz','w:gz') as target:
    for item in source:
        if not item.isfile() or item.name in replace: continue
        data=source.extractfile(item).read();target.addfile(item,io.BytesIO(data))
        entries.append((item.name,hashlib.sha256(data).hexdigest()))
    for name in ['dovedex-viewer','dovedex-direct-host']:
        data=(p/name).read_bytes();item=tarfile.TarInfo(name);item.size=len(data);item.mode=0o700
        target.addfile(item,io.BytesIO(data));entries.append((name,hashlib.sha256(data).hexdigest()))
    data=''.join(h+'  '+n+'\n' for n,h in entries).encode()
    item=tarfile.TarInfo('contents.sha256');item.size=len(data);target.addfile(item,io.BytesIO(data))
h=hashlib.sha256((p/'runtime.tar.gz').read_bytes()).hexdigest()
f=p/'backend.sh';f.write_bytes(re.sub(rb'expected=[0-9a-f]{64}',b'expected='+h.encode(),f.read_bytes()))
names=['runtime.tar.gz','backend.sh','native-decoder.sh','x11-bridge']
(p/'direct.sha256').write_bytes(''.join(hashlib.sha256((p/n).read_bytes()).hexdigest()+'  '+n+'\n' for n in names).encode())
print('Direct runtime:',h)
