from pathlib import Path
import re,subprocess,shutil,json,hashlib
q=Path('/mnt/c/vscode/HPlapdock/doveos_v2/doveplayer')
original=Path('/home/hakami55/doveos-mpv-trial-20261007/bundle')
out=Path('/home/hakami55/doveplayer-runtime');(out/'lib').mkdir(parents=True,exist_ok=True)
queue=[q/'doveplayer-core'];seen=set();manifest=[]
while queue:
    p=queue.pop(0)
    if p.name in seen:continue
    seen.add(p.name)
    needed=re.findall(r'Shared library: \[(.*?)\]',subprocess.check_output(['readelf','-d',str(p)],text=True))
    if p.name!='doveplayer-core':shutil.copyfile(p,out/'lib'/p.name)
    for n in needed:
        path=original/'lib'/n
        if not path.is_file():raise RuntimeError(n)
        queue.append(path)
    manifest.append({'file':p.name,'needed':needed,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
shutil.copytree(original/'copyright',out/'copyright',dirs_exist_ok=True)
shutil.copyfile(original/'packages.json',out/'packages.json')
(out/'dependencies.json').write_text(json.dumps(manifest,indent=2))
size=sum(p.stat().st_size for p in out.rglob('*') if p.is_file())
image=Path('/home/hakami55/doveplayer-runtime.ext2')
subprocess.run(['truncate','-s',str((size//1048576+12)*1048576),str(image)],check=True)
subprocess.run(['mkfs.ext2','-q','-F','-d',str(out),str(image)],check=True)
shutil.copyfile(image,q/'runtime.ext2')
digest=hashlib.sha256(image.read_bytes()).hexdigest()
(q/'runtime.sha256').write_text(digest+'  runtime.ext2\n')
(q/'dependencies.json').write_text(json.dumps(manifest,indent=2))
print('PLAYER_RUNTIME',len(seen),size,digest,flush=True)
