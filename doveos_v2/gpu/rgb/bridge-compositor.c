/* Bounded opaque native-GPU window composition candidate.
 * Own context/FBO and imported app images; root texture storage stays owned
 * by glamor. The X server thread serialises root writes and finishes them.
 */
#define _GNU_SOURCE
#include "../native-window/raw-native.h"
#define DOVE_SHARED_LIBRARY
#include "../native-window/shared-window.c"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
typedef struct {uint32_t version,width,height,frames,verify,window;} Hello;
typedef struct {
 Target source[3];Hello hello;unsigned image_width,image_height;
 int peer,peer_pid,imports;unsigned sequence,materialized;uint64_t materialized_hash;size_t bytes;double opened;
} Peer;
static Peer peers[4],*active;
static size_t imported_bytes;
#define MAX_IMPORTED_BYTES (32u*1024*1024)
static struct {
 void*mali;Display*host;EGLDisplay display;EGLContext context;EGLSurface surface;
 GLuint root_fbo,program;
 int listen,failed,bound;char path[108];
 int(*window_info)(unsigned,int,int*,int*,int*);void(*watch)(int,int);GLint destination,source_scale;
} bridge={.listen=-1};
typedef struct {EGLDisplay d;EGLContext c;EGLSurface draw,read;} Saved;
static Saved save(void){return (Saved){eglGetCurrentDisplay(),eglGetCurrentContext(),eglGetCurrentSurface(EGL_DRAW),eglGetCurrentSurface(EGL_READ)};}
static int enter(void){return eglMakeCurrent(bridge.display,bridge.surface,bridge.surface,bridge.context);}
static int restore(Saved*s){return eglMakeCurrent(s->d,s->draw,s->read,s->c);}
static void release_peer(void){
 glFinish();for(int i=0;i<3;i++){if(active->source[i].x)close_target(&active->source[i]);memset(&active->source[i],0,sizeof active->source[i]);}
 if(active->peer>=0){bridge.watch(active->peer,0);close(active->peer);}imported_bytes-=active->bytes;memset(active,0,sizeof *active);active->peer=-1;
}
static void close_current(void){
 for(int i=0;i<4;i++){active=&peers[i];release_peer();}if(bridge.root_fbo)glDeleteFramebuffers(1,&bridge.root_fbo);if(bridge.program)glDeleteProgram(bridge.program);
 if(bridge.listen>=0){bridge.watch(bridge.listen,0);close(bridge.listen);bridge.listen=-1;if(bridge.bound)unlink(bridge.path);bridge.bound=0;}
}
int dove_compositor_setup(void*host,unsigned root_texture,unsigned width,unsigned height,int(*window_info)(unsigned,int,int*,int*,int*),void(*watch)(int,int)){
 if(bridge.context)return 1;if(width!=1280||height!=720)return 0;
 for(int i=0;i<4;i++)peers[i].peer=-1;
 const char*path=getenv("DOVE_COMPOSITOR_SOCKET");if(!path||strlen(path)>=sizeof bridge.path)return 0;
 bridge.mali=dlopen(getenv("DOVE_NATIVE_MALI"),RTLD_NOW|RTLD_LOCAL);if(!bridge.mali||!raw_load(bridge.mali)){fprintf(stderr,"COMPOSITOR_NATIVE_LOAD_FAILED %s\n",dlerror());return 0;}
 Saved original=save();if(!original.d||!original.c||!window_info){fprintf(stderr,"COMPOSITOR_CONTEXT_MISSING d=%p c=%p info=%p\n",original.d,original.c,window_info);return 0;}glFinish();bridge.display=original.d;bridge.host=host;bridge.window_info=window_info;bridge.watch=watch;
 EGLConfig cfg;EGLint n;EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
 if(!eglChooseConfig(bridge.display,ca,&cfg,1,&n)||!n)return 0;
 EGLint pa[]={EGL_WIDTH,32,EGL_HEIGHT,32,EGL_NONE},a[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
 bridge.surface=eglCreatePbufferSurface(bridge.display,cfg,pa);bridge.context=eglCreateContext(bridge.display,cfg,original.c,a);
 if(!bridge.surface||!bridge.context||!enter())goto failed;
 glGenFramebuffers(1,&bridge.root_fbo);glBindFramebuffer(GL_FRAMEBUFFER,bridge.root_fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,root_texture,0);
 if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)goto failed;
 GLuint v=shader(GL_VERTEX_SHADER,"attribute vec2 p;uniform vec4 destination;uniform vec2 source_scale;varying vec2 uv;void main(){vec2 q=p*.5+.5;gl_Position=vec4(destination.xy+q*destination.zw,0.,1.);uv=vec2(q.x,1.-q.y)*source_scale;}"),f=shader(GL_FRAGMENT_SHADER,"precision mediump float;varying vec2 uv;uniform sampler2D image;void main(){gl_FragColor=texture2D(image,uv);}");
 if(!v||!f)goto failed;bridge.program=glCreateProgram();glAttachShader(bridge.program,v);glAttachShader(bridge.program,f);glBindAttribLocation(bridge.program,0,"p");glLinkProgram(bridge.program);glDeleteShader(v);glDeleteShader(f);
 GLint linked;glGetProgramiv(bridge.program,GL_LINK_STATUS,&linked);if(!linked)goto failed; static GLfloat quad[]={-1,-1,1,-1,-1,1,1,1};glVertexAttribPointer(0,2,GL_FLOAT,0,0,quad);glEnableVertexAttribArray(0);glUseProgram(bridge.program);glUniform1i(glGetUniformLocation(bridge.program,"image"),0);bridge.destination=glGetUniformLocation(bridge.program,"destination");glActiveTexture(GL_TEXTURE0);
 bridge.source_scale=glGetUniformLocation(bridge.program,"source_scale");
 bridge.listen=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(bridge.listen<0)goto failed;
 struct sockaddr_un address={.sun_family=AF_UNIX};strcpy(address.sun_path,path);strcpy(bridge.path,path);
 mode_t old=umask(077);int ok=!bind(bridge.listen,(void*)&address,sizeof address);umask(old);bridge.bound=ok;if(!ok||listen(bridge.listen,4))goto failed;
 if(!restore(&original))goto failed;bridge.watch(bridge.listen,1);
 fprintf(stderr,"COMPOSITOR_BRIDGE_READY peers=4 byte_budget=33554432 width=%u height=%u\n",width,height);return 1;
failed:
 fprintf(stderr,"COMPOSITOR_SETUP_FAILED egl=%x gl=%x\n",eglGetError(),glGetError());
 if(bridge.context&&enter())close_current();eglMakeCurrent(bridge.display,0,0,0);if(bridge.context)eglDestroyContext(bridge.display,bridge.context);if(bridge.surface)eglDestroySurface(bridge.display,bridge.surface);
 bridge.context=0;bridge.surface=0;restore(&original);return 0;
}
static int import_message(void){
 Hello hello;char control[CMSG_SPACE(sizeof(int)*8)]={0};struct iovec i={&hello,sizeof hello};struct msghdr m={.msg_iov=&i,.msg_iovlen=1,.msg_control=control,.msg_controllen=sizeof control};
 ssize_t n=recvmsg(active->peer,&m,MSG_DONTWAIT|MSG_CMSG_CLOEXEC);if(n<0&&(errno==EAGAIN||errno==EINTR))return 0;if(!n)return -1;
 int fd=-1,descriptors=0,bad_control=0;
 for(struct cmsghdr*c=CMSG_FIRSTHDR(&m);c;c=CMSG_NXTHDR(&m,c)){
  if(c->cmsg_level!=SOL_SOCKET||c->cmsg_type!=SCM_RIGHTS||c->cmsg_len<CMSG_LEN(0)){bad_control=1;continue;}
  size_t bytes=c->cmsg_len-CMSG_LEN(0);if(bytes%sizeof(int))bad_control=1;
  for(size_t z=0;z+sizeof(int)<=bytes;z+=sizeof(int)){int got;memcpy(&got,(char*)CMSG_DATA(c)+z,sizeof got);descriptors++;if(fd<0)fd=got;else close(got);}
 }
 if(n!=sizeof hello||(m.msg_flags&(MSG_CTRUNC|MSG_TRUNC))||bad_control||descriptors!=1){if(fd>=0)close(fd);return -3;}
 int geometry[4],rects[1024],count;int visible=bridge.window_info(hello.window,active->peer_pid,geometry,rects,&count);
 int ok=(hello.version==5||hello.version==6)&&hello.width>=32&&hello.width<=1280&&hello.height>=32&&hello.height<=720&&(hello.frames==0||(hello.frames>=4&&hello.frames<=3601))&&visible>=0;
 if(ok)ok=hello.version==6?(geometry[2]>0&&geometry[3]>0&&(unsigned)geometry[2]<=hello.width&&(unsigned)geometry[3]<=hello.height):((unsigned)geometry[2]==hello.width&&(unsigned)geometry[3]==hello.height);
 if(active->imports)ok=ok&&!memcmp(&active->hello,&hello,sizeof hello);else active->hello=hello;
 for(int j=0;j<4;j++)if(&peers[j]!=active&&peers[j].imports&&peers[j].hello.window==hello.window)ok=0;
 size_t bytes=(((size_t)hello.width*hello.height*4)+4095)&~(size_t)4095;
 if(!ok||bytes>MAX_IMPORTED_BYTES-imported_bytes){close(fd);return -3;}
 import_dma=fd;gpu_only=1;ok=create_target(&active->source[active->imports],bridge.display,bridge.host,hello.width,hello.height);import_dma=-1;close(fd);if(!ok){if(active->source[active->imports].x)close_target(&active->source[active->imports]);memset(&active->source[active->imports],0,sizeof(Target));return -3;}
 active->bytes+=bytes;imported_bytes+=bytes;active->imports++;if(active->imports==3){active->image_width=hello.width;active->image_height=hello.height;uint32_t ack=0;if(send(active->peer,&ack,sizeof ack,MSG_NOSIGNAL)!=sizeof ack)return -2;}
 return 0;
}
static int verify_root(Target*t,unsigned frame,int*geometry,int*rects,int count){
 size_t bytes=1280u*720u*4;unsigned char*p=malloc(bytes);if(!p)return 0;glReadPixels(0,0,1280,720,GL_RGBA,GL_UNSIGNED_BYTE,p);glFinish();unsigned bad=0;
 for(int i=0;i<count;i++){int*r=rects+i*4;for(int y=r[1];y<r[1]+r[3];y++)for(int x=r[0];x<r[0]+r[2];x++)for(int k=0;k<4;k++){
  int sx=x-geometry[0],sy=y-geometry[1],b=k==0?2:k==2?0:k;
  if(sx<0||sx>=geometry[2]||sy<0||sy>=geometry[3]){bad++;continue;}
  sx=(int)((sx+.5)*active->image_width/geometry[2]);sy=(int)((sy+.5)*active->image_height/geometry[3]);
  if(p[((size_t)y*1280+x)*4+k]!=((unsigned char*)t->cpu)[((size_t)(active->image_height-sy-1)*t->w+sx)*4+b])bad++;
 }}
 free(p);fprintf(stderr,"COMPOSITOR_ROOT_COMPARE frame=%u bad_bytes=%u\n",frame,bad);return !bad;
}
static int tick_peer(int redraw){
 if(!bridge.context||bridge.failed)return bridge.failed?-1:0;
 if(active->peer<0)return 0;

 Saved original=save();glFinish();if(!enter()){bridge.failed=1;return -1;}int result=0;
 if(active->imports<3&&now()-active->opened>5){result=-3;goto ended;}
 if(active->imports<3){result=import_message();if(result<0)goto ended;restore(&original);return 0;}
 int fresh=1;uint32_t packet[3]={0};ssize_t n=recv(active->peer,packet,sizeof packet,MSG_DONTWAIT|MSG_TRUNC);uint32_t frame=packet[0];
 if(n<0&&(errno==EAGAIN||errno==EINTR)){
  if(!redraw||!active->sequence)goto returned;
  fresh=0;frame=active->sequence;
 }else if(!n){result=-1;goto ended;}
 if((fresh&&(n!=(ssize_t)(active->hello.version==6?sizeof packet:sizeof frame)||frame!=active->sequence+1||(active->hello.frames&&frame>active->hello.frames)))||!thermal_ok()){result=-2;goto ended;}
 if(fresh&&active->hello.version==6){if(packet[1]<1||packet[2]<1||packet[1]>active->hello.width||packet[2]>active->hello.height){result=-2;goto ended;}active->image_width=packet[1];active->image_height=packet[2];}
 int geometry[4],rects[1024],count=0;int visible=bridge.window_info(active->hello.window,active->peer_pid,geometry,rects,&count);
 if(visible<0||(active->hello.version==5&&((unsigned)geometry[2]!=active->hello.width||(unsigned)geometry[3]!=active->hello.height))){result=-3;goto ended;}
 uint64_t hash=1469598103934665603ULL;
 for(int j=0;j<4;j++)hash=(hash^(unsigned)geometry[j])*1099511628211ULL;
 for(int j=0;j<count*4;j++)hash=(hash^(unsigned)rects[j])*1099511628211ULL;
 if(redraw==2&&!fresh&&active->materialized==frame&&active->materialized_hash==hash)goto returned;
 if(redraw==1)active->materialized=0;
 /* The latest source remains leased until a subsequent frame completes. */
 Target*t=&active->source[(frame-1)%3];
 if(getenv("DOVE_COMPOSITOR_OUTPUT")&&redraw!=2&&!getenv("DOVE_COMPOSITOR_VERIFY")){
  /* The preceding RGB frame has completed its fence on this same X thread.
   * Ack f releases f-1; the new latest image stays leased. No root copy. */
  active->sequence=frame;
  if(fresh&&send(active->peer,&frame,sizeof frame,MSG_NOSIGNAL)!=sizeof frame){result=-2;goto ended;}
  result=1;goto returned;
 }
 glBindFramebuffer(GL_FRAMEBUFFER,bridge.root_fbo);glViewport(0,0,1280,720);glEnable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glUseProgram(bridge.program);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,t->texture);
 glUniform4f(bridge.destination,2.f*geometry[0]/1280-1.f,2.f*geometry[1]/720-1.f,2.f*geometry[2]/1280,2.f*geometry[3]/720);
 glUniform2f(bridge.source_scale,(float)active->image_width/active->hello.width,(float)active->image_height/active->hello.height);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
 if(visible)for(int i=0;i<count;i++){int*r=rects+i*4;if(r[0]<0||r[1]<0||r[2]<1||r[3]<1||r[2]>1280-r[0]||r[3]>720-r[1]){result=-2;goto ended;}glScissor(r[0],r[1],r[2],r[3]);glDrawArrays(GL_TRIANGLE_STRIP,0,4);}
 glFinish();glDisable(GL_SCISSOR_TEST);active->materialized=frame;active->materialized_hash=hash;
 if(glGetError()!=GL_NO_ERROR||((getenv("DOVE_COMPOSITOR_VERIFY")&&(active->hello.verify||frame==1||frame==active->hello.frames))&&!verify_root(t,frame,geometry,rects,visible?count:0))){bridge.failed=1;result=-2;goto ended;}
 active->sequence=frame;if(fresh&&send(active->peer,&frame,sizeof frame,MSG_NOSIGNAL)!=sizeof frame){result=-2;goto ended;}result=1;goto returned;
ended:
 fprintf(stderr,"COMPOSITOR_PEER_END frames=%u code=%d\n",active->sequence,result);release_peer();result=bridge.failed?-1:0;
returned:
 if(!restore(&original))return -1;return result;
}
int dove_compositor_tick(int redraw){
 if(!bridge.context||bridge.failed)return bridge.failed?-1:0;
 for(int j=0;j<4;j++){
  int fd=accept4(bridge.listen,NULL,NULL,SOCK_NONBLOCK|SOCK_CLOEXEC);if(fd<0)break;
  struct ucred peer;socklen_t size=sizeof peer;int slot=-1;
  for(int i=0;i<4;i++)if(peers[i].peer<0){slot=i;break;}
  if(slot<0||getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&size)||peer.uid!=geteuid()){close(fd);continue;}
  active=&peers[slot];active->peer=fd;active->peer_pid=peer.pid;active->opened=now();bridge.watch(fd,1);
 }
 int changed=0;
 for(int i=0;i<4;i++)if(peers[i].peer>=0){active=&peers[i];int r=tick_peer(redraw);if(r<0)return r;changed|=r;}
 return changed;
}
int dove_compositor_output_rects(int*rects,int capacity){
 if(!bridge.context||bridge.failed||!getenv("DOVE_COMPOSITOR_OUTPUT"))return 0;
 int total=0;
 for(int i=0;i<4;i++){
  Peer*p=&peers[i];if(p->peer<0||p->imports!=3||!p->sequence)continue;
  int geometry[4],clips[1024],count=0;
  int visible=bridge.window_info(p->hello.window,p->peer_pid,geometry,clips,&count);
  if(visible<=0||(p->hello.version==5&&((unsigned)geometry[2]!=p->hello.width||(unsigned)geometry[3]!=p->hello.height)))continue;
  if(count>capacity-total)return -1;
  memcpy(rects+total*4,clips,count*4*sizeof(int));total+=count;
 }
 return total;
}
int dove_compositor_output_draw(int left,int top,int right,int bottom){
 if(!bridge.context||bridge.failed||!getenv("DOVE_COMPOSITOR_OUTPUT"))return 1;
 static GLfloat quad[]={-1,-1,1,-1,-1,1,1,1};
 for(int i=0;i<4;i++){
  Peer*p=&peers[i];if(p->peer<0||p->imports!=3||!p->sequence)continue;
  int geometry[4],clips[1024],count=0;
  int visible=bridge.window_info(p->hello.window,p->peer_pid,geometry,clips,&count);
  if(visible<=0||(p->hello.version==5&&((unsigned)geometry[2]!=p->hello.width||(unsigned)geometry[3]!=p->hello.height)))continue;
  glViewport(0,0,1280,720);
  glEnable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);
  glUseProgram(bridge.program);glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D,p->source[(p->sequence-1)%3].texture);
  glVertexAttribPointer(0,2,GL_FLOAT,0,0,quad);glEnableVertexAttribArray(0);
  glUniform4f(bridge.destination,2.f*geometry[0]/1280-1.f,2.f*geometry[1]/720-1.f,2.f*geometry[2]/1280,2.f*geometry[3]/720);
  glUniform2f(bridge.source_scale,(float)p->image_width/p->hello.width,(float)p->image_height/p->hello.height);
  for(int j=0;j<count;j++){int*r=clips+j*4;int x=r[0]>left?r[0]:left,y=r[1]>top?r[1]:top;int endx=r[0]+r[2]<right?r[0]+r[2]:right,endy=r[1]+r[3]<bottom?r[1]+r[3]:bottom;if(x<endx&&y<endy){glScissor(x,y,endx-x,endy-y);glDrawArrays(GL_TRIANGLE_STRIP,0,4);}}
 }
 return glGetError()==GL_NO_ERROR;
}
void dove_compositor_close(void){
 if(!bridge.context)return;Saved original=save();if(enter())close_current();eglMakeCurrent(bridge.display,0,0,0);eglDestroyContext(bridge.display,bridge.context);eglDestroySurface(bridge.display,bridge.surface);bridge.context=0;bridge.surface=0;restore(&original);fprintf(stderr,"COMPOSITOR_BRIDGE_CLOSED\n");
}
