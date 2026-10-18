"""Generate the native receiver integrity manifest with Linux line endings."""
from pathlib import Path
import hashlib
b=Path(__file__).resolve().parent
names=['native-player.mjs','native-receiver.mjs','media-source.mjs','vision-source.mjs',
       'safe-diagnostics.mjs','cast-player','cast-direct.sh','receiver.sh','youtube-ui',
       'receiver-run','launch.sh','yt-lite.tar.gz']
for name in names:
    p=b/name
    if p.suffix in ('.sh','.mjs'):
        p.write_bytes(p.read_bytes().replace(b'\r\n',b'\n'))
(b/'receiver.sha256').write_bytes(''.join(
    hashlib.sha256((b/name).read_bytes()).hexdigest()+'  '+name+'\n'
    for name in names).encode())
print('RECEIVER_MANIFEST_READY',len(names))
