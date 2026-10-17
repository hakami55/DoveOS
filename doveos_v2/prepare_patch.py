from pathlib import Path
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
WORK = Path('/home/hakami55/doveos-v2-build')
PATCH = WORK / 'desktop-patch'
PATCH.mkdir(exist_ok=True)
for part in ('usr/lib/menu-cache','usr/lib/X11','usr/lib/jwm','etc/X11'):
    source=WORK/'desktop-extracted'/part
    if source.exists():
        (PATCH/part).mkdir(parents=True,exist_ok=True)
        subprocess.run(['rsync','-a',str(source)+'/',str(PATCH/part)+'/'],check=True)
        (WORK/'desktop-runtime'/part).mkdir(parents=True,exist_ok=True)
        subprocess.run(['rsync','-a',str(source)+'/',str(WORK/'desktop-runtime'/part)+'/'],check=True)
subprocess.run(['rsync','-a',str(HERE/'overlay')+'/',str(PATCH)+'/'],check=True)
subprocess.run(['update-mime-database',str(WORK/'desktop-runtime/usr/share/mime')],check=True)
(PATCH/'usr/share/mime').mkdir(parents=True,exist_ok=True)
subprocess.run(['rsync','-a',str(WORK/'desktop-runtime/usr/share/mime')+'/',str(PATCH/'usr/share/mime')+'/'],check=True)
for file in (PATCH/'usr/bin').glob('doveos-*'):
    file.chmod(0o755)
subprocess.run(['tar','-czf',str(HERE/'artifacts/desktop-patch.tar.gz'),'-C',str(PATCH),'.'],check=True)
