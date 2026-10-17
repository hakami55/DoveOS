from pathlib import Path
import json,hashlib
q=Path(__file__).resolve().parent
r=q.parents[1]
s=r/'analysis/gpu-integration-20261007'
copies={'broker.c':s/'vlc-queued-host.c','decoder.h':s/'vlc-queued-omx.h','video-ipc.h':s/'vlc_rtk_protocol.h','audio-host.c':s/'audio-host.c','audio-ipc.h':s/'audio-ipc.h','hw_output.h':r/'moonlight_dock_prototype/hw_output.h'}
manifest={}
for name,p in copies.items():
    data=p.read_bytes();manifest[name]={'source':str(p.relative_to(r)),'sha256':hashlib.sha256(data).hexdigest()}
    text=data.decode().replace('\r\n','\n')
    text=text.replace('"vlc_rtk_protocol.h"','"video-ipc.h"').replace('"vlc-queued-omx.h"','"decoder.h"')
    text=text.replace('/tmp/overnight-vlc-shared/rtk-direct.sock','/tmp/doveplayer/video.sock').replace('/tmp/gpu-integration-check/audio.sock','/tmp/doveplayer/audio.sock')
    text=text.replace('DIRECT_VLC','DOVEPLAYER').replace('alarm(60);','alarm(0);').replace('alarm(180);','alarm(0);')
    if name=='broker.c':
        text=text.replace('prctl(PR_SET_PDEATHSIG,SIGKILL);','prctl(PR_SET_PDEATHSIG,SIGTERM);')
        text=text.replace('if(child>0){int status;', 'if(child>0){FILE *pidfile=fopen("/tmp/doveplayer/broker-child.pid","w");if(pidfile){fprintf(pidfile,"%ld\\n",(long)child);fclose(pidfile);}int status;')
        text=text.replace('WTERMSIG(status));}', 'WTERMSIG(status));unlink("/tmp/doveplayer/broker-child.pid");}')
    if name=='hw_output.h':
        # Preserve the decoder buffers if retirement fails. Never release the display lease.
        text=text.replace('if(rc==0x10000000u)for', 'if(rc!=0x10000000u){puts("DOVEPLAYER_RETAINED_UNRETIRED_BUFFERS");fflush(stdout);alarm(0);signal(SIGTERM,SIG_IGN);signal(SIGINT,SIG_IGN);for(;;)sleep(60);}\n        if(rc==0x10000000u)for')
    (q/name).write_bytes(text.encode())
(q/'reused-sources.json').write_text(json.dumps(manifest,indent=2))
