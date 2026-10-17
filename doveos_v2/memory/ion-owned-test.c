#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <errno.h>
struct allocation{uint32_t len,align,mask,flags,handle;};
struct share{uint32_t handle;int fd;};
/* This firmware's ioctl encodes 12 bytes but copies its 20-byte union. */
struct physical{uint32_t handle,address,len,reserved[2];};
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
int main(int argc,char**argv){
 setvbuf(stdout,NULL,_IONBF,0);alarm(40);
 unsigned mib=argc>1?strtoul(argv[1],0,10):1;if(mib<1||mib>64)return 2;
 int ion=open("/dev/ion",O_RDWR|O_CLOEXEC);if(ion<0){perror("ion");return 3;}
 struct allocation a={mib*1024*1024,4096,0x80,0xf0000000,0};
 if(ioctl(ion,0xc0144900,&a)){perror("allocate");close(ion);return 4;}
 struct physical p={a.handle,0,0,{0,0}};int result=0;struct share s={a.handle,-1};void*map=MAP_FAILED;
 if(ioctl(ion,0xc00c4908,&p)){perror("physical");result=5;goto out;}
 printf("OWNED_ION bytes=%u physical=%08x length=%u handle=%u\n",a.len,p.address,p.len,a.handle);
 // Only memory returned by the allocator, fully inside a known System RAM pool.
 if(p.len!=a.len||p.address<0x10100000||((uint64_t)p.address+p.len)>0x18000000){puts("ALLOCATION_OUTSIDE_TEST_POOL_REFUSED");result=6;goto out;}
 if(ioctl(ion,0xc0084904,&s)){perror("share");result=7;goto out;}
 map=mmap(0,a.len,PROT_READ|PROT_WRITE,MAP_SHARED,s.fd,0);if(map==MAP_FAILED){perror("mmap");result=8;goto out;}
 volatile uint32_t*q=map;unsigned words=a.len/4;double start=now();
 for(unsigned i=0;i<words;i++)q[i]=(i*2654435761u)^0xa55a392bu;
 for(unsigned i=0;i<words;i++)if(q[i]!=((i*2654435761u)^0xa55a392bu)){printf("MISMATCH word=%u\n",i);result=9;goto out;}
 printf("OWNED_BUFFER_ROUNDTRIP_PASS mib=%u seconds=%.3f\n",mib,now()-start);
out:
 if(map!=MAP_FAILED)munmap(map,a.len);
 if(s.fd>=0)close(s.fd);
 if(ioctl(ion,0xc0044901,&a.handle)){perror("free");result=10;}close(ion);
 puts("OWNED_ION_RELEASED");return result;
}
