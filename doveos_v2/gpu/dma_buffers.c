/* Only translate imports of buffers explicitly owned by this process. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
typedef struct {int fd;uint32_t phys,size;} Buffer;
typedef struct {int fd;uint32_t cookie;} Import;
static Buffer buffers[32];static Import imports[64];
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
void gpu_share_buffer(int fd,uint32_t physical,uint32_t bytes){
 pthread_mutex_lock(&lock);
 for(unsigned i=0;i<32;i++)if(!buffers[i].size||buffers[i].phys==physical){buffers[i]=(Buffer){fd,physical,bytes};break;}
 pthread_mutex_unlock(&lock);
}
void gpu_forget_buffer(uint32_t physical){
 pthread_mutex_lock(&lock);for(unsigned i=0;i<32;i++)if(buffers[i].phys==physical)buffers[i].size=0;pthread_mutex_unlock(&lock);
}
int ioctl(int fd,unsigned long req,...){
 static int(*real)(int,unsigned long,...);if(!real)real=dlsym(RTLD_NEXT,"ioctl");
 va_list ap;va_start(ap,req);uint32_t *p=va_arg(ap,void*);va_end(ap);
 if(req==0xc020830b){
  pthread_mutex_lock(&lock);Buffer b={0};for(unsigned i=0;i<32;i++)if(buffers[i].size&&p[2]==buffers[i].phys&&p[3]==buffers[i].size){b=buffers[i];break;}
  if(b.size){
   unsigned slot=0;while(slot<64&&imports[slot].cookie)slot++;
   if(slot==64){pthread_mutex_unlock(&lock);errno=ENOMEM;return -1;}
   uint32_t q[8]={p[0],p[1],b.fd,b.size,p[4],0,0,0};int r=real(fd,0xc0208306,q);
   if(!r){p[7]=q[7];imports[slot]=(Import){fd,q[7]};}
   pthread_mutex_unlock(&lock);return r;
  }pthread_mutex_unlock(&lock);
 }
 if(req==0x4010830c){
  pthread_mutex_lock(&lock);
  for(unsigned i=0;i<64;i++)if(imports[i].cookie&&imports[i].fd==fd&&imports[i].cookie==p[2]){
   int r=real(fd,0x40108307,p);if(!r)imports[i].cookie=0;pthread_mutex_unlock(&lock);return r;
  }pthread_mutex_unlock(&lock);
 }
 return real(fd,req,p);
}
