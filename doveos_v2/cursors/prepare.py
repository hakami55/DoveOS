from pathlib import Path
import struct, shutil, tarfile, hashlib
ROOT=Path(__file__).resolve().parents[2]
P=Path(__file__).resolve().parent
C=ROOT/'doveos_v2/third_party/Chicago95/Cursors'
theme=P/'package/DoveClassicCursor'
theme.mkdir(parents=True,exist_ok=True)
shutil.copytree(C/'Chicago95_Standard_Cursors/cursors',theme/'cursors',dirs_exist_ok=True)
for f in (C/'Chicago95_Animated_Hourglass_Cursors/cursors').iterdir():
    shutil.copyfile(f,theme/'cursors'/f.name)
(theme/'index.theme').write_bytes(b'[Icon Theme]\nName=DoveOS DoveOS\nComment=Classic white pointers and animated hourglasses\n')
def images(name):
    b=(theme/'cursors'/name).read_bytes(); magic,header,version,n=struct.unpack_from('<4I',b)
    assert magic==0x72756358
    out=[]
    for i in range(n):
        typ,size,pos=struct.unpack_from('<3I',b,16+12*i)
        if typ!=0xfffd0002 or size!=32:continue
        h,t,sz,v,w,ht,hx,hy,delay=struct.unpack_from('<9I',b,pos)
        pixels=struct.unpack_from('<%dI'%(w*ht),b,pos+h)
        out.append((w,ht,hx,hy,delay,pixels))
    assert out
    return out
frames=images('left_ptr_watch')
text='/* Chicago95 cursor frames; see accompanying upstream license. */\n'
text+='struct BusyFrame {int w,h,hx,hy,delay;const uint32_t *pixels;};\n'
for i,f in enumerate(frames):
    text+='static const uint32_t busy_pixels_%d[]={%s};\n'%(i,','.join(hex(x)+'u' for x in f[-1]))
text+='static const struct BusyFrame busy_frames[]={'+','.join('{%d,%d,%d,%d,%d,busy_pixels_%d}'%(*f[:5],i) for i,f in enumerate(frames))+'};\n'
(P/'busy-frames.h').write_bytes(text.encode())
s=(ROOT/'analysis/gpu-integrate-20261003/gpu-cursor.c').read_text()
s=s.replace('#include <unistd.h>','#include <unistd.h>\n#include <time.h>\n#include <dirent.h>\n#include <sys/stat.h>\n#include "busy-frames.h"')
s=s.replace('int main(int argc', '''static uint32_t clock_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint32_t)(t.tv_sec*1000u+t.tv_nsec/1000000u);}
static int launch_busy(void){
 DIR *d=opendir("/tmp/doveos-cursor-busy");int busy=0;
 if(d){struct dirent *e;int checked=0;while((e=readdir(d))&&checked++<64){
  char path[320];long pid=0;unsigned deadline=0;
  if(e->d_name[0]=='.')continue;
  snprintf(path,sizeof path,"/tmp/doveos-cursor-busy/%s",e->d_name);
  FILE *f=fopen(path,"r");if(!f)continue;
  int n=fscanf(f,"%ld %u",&pid,&deadline);fclose(f);
  if(n==2&&pid>1&&pid<4194304&&!kill(pid,0)&&(int32_t)(deadline-clock_ms())>0){busy=1;break;}
 }closedir(d);}
 if(!busy){struct stat st;FILE *f=fopen("/tmp/doveos-vlc-opening","r");long pid=0;
  if(f){int n=fscanf(f,"%ld",&pid);fclose(f);busy=n==1&&pid>1&&pid<4194304&&!kill(pid,0)&&!stat("/tmp/doveos-vlc-opening",&st)&&time(NULL)-st.st_mtime<60;}
 }return busy;
}
int main(int argc''')
s=s.replace('unsigned moves=0;', 'unsigned moves=0;int busy=0,last_busy=-1,last_frame=-1;uint32_t busy_check=0;')
s=s.replace('if(paused){paused=0;w=h=0;serial=0;', 'if(paused){paused=0;w=h=0;serial=0;last_busy=-1;last_frame=-1;')
key='  int nx=c->x-c->xhot,ny=c->y-c->yhot;'
replacement='''  uint32_t now=clock_ms();if((uint32_t)(now-busy_check)>=100){busy=launch_busy();busy_check=now;}
  int frame=busy?(now/150)%(sizeof busy_frames/sizeof busy_frames[0]):-1;
  if(busy){const struct BusyFrame *b=&busy_frames[frame];
   if(b->w*b->h<=c->width*c->height){c->width=b->w;c->height=b->h;c->xhot=b->hx;c->yhot=b->hy;
    for(int i=0;i<b->w*b->h;i++)c->pixels[i]=b->pixels[i];
   }else{XFree(c);usleep(16000);continue;}
  }
  int nx=c->x-c->xhot,ny=c->y-c->yhot;'''
# XFixes allocation may be smaller than busy artwork; use separate draw buffer.
replacement=replacement.replace('if(b->w*b->h<=c->width*c->height){','{').replace('for(int i=0;i<b->w*b->h;i++)c->pixels[i]=b->pixels[i];','').replace('}else{XFree(c);usleep(16000);continue;}','}')
s=s.replace(key,replacement)
s=s.replace('serial!=c->cursor_serial||!moves','serial!=c->cursor_serial||busy!=last_busy||frame!=last_frame||!moves')
s=s.replace('serial=c->cursor_serial;', 'serial=c->cursor_serial;last_busy=busy;last_frame=frame;')
s=s.replace('(uint32_t)c->pixels[j*w+i]', 'busy?busy_frames[frame].pixels[j*w+i]:(uint32_t)c->pixels[j*w+i]')
(P/'gpu-cursor.c').write_bytes(s.encode())
if not (P/'open_app.before.c').exists():shutil.copyfile(ROOT/'doveos_v2/open_app.c',P/'open_app.before.c')
s=(P/'open_app.before.c').read_text().replace('#include <unistd.h>','#include <unistd.h>\n#include <time.h>\n#include <sys/stat.h>')
key=' int status;while(waitpid(child,&status,0)<0)'
new=''' char busy_path[160];snprintf(busy_path,sizeof busy_path,"/tmp/doveos-cursor-busy/%ld",(long)child);
 mkdir("/tmp/doveos-cursor-busy",0700);
 int status=0,finished=0;
 for(int n=0;n<75;n++){
  pid_t r=waitpid(child,&status,WNOHANG);if(r==child){finished=1;break;}
  Display *d=XOpenDisplay(NULL);int ready=d&&find(d,id);if(d)XCloseDisplay(d);if(ready)break;
  if(n==1){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);unsigned deadline=(unsigned)(t.tv_sec*1000u+t.tv_nsec/1000000u)+15000;
   FILE *b=fopen(busy_path,"w");if(b){fprintf(b,"%ld %u\\n",(long)child,deadline);fclose(b);}
  }
  usleep(200000);
 }
 unlink(busy_path);
 if(finished){close(fd);return WIFEXITED(status)?WEXITSTATUS(status):1;}
 while(waitpid(child,&status,0)<0)'''
assert key in s;s=s.replace(key,new)
(P/'open_app.c').write_bytes(s.encode())
print('Theme assets and launch feedback source prepared',len(frames),'busy frames')
