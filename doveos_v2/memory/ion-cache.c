#define _GNU_SOURCE
#define FUSE_USE_VERSION 26
#define _FILE_OFFSET_BITS 64
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <time.h>

/* Experimental read-only asset cache. All physical memory is owned through
 * the existing ION allocator. Never mmap or write arbitrary physical RAM. */
#define CAPACITY (32u*1024*1024)
#define MAX_FILES 4096
struct allocation { uint32_t len,align,mask,flags,handle; };
struct share { uint32_t handle; int fd; };
/* Vendor ABI bug: request encodes 12 bytes, actual copy is 20 bytes. */
struct physical { uint32_t handle,address,len,padding[2]; };
struct header { char magic[8]; uint32_t count,size; };
struct entry { char path[256]; uint32_t offset,size,mode; };
static struct entry *entries;
static unsigned count,packsize;
static int ion=-1,shared=-1;
static uint32_t handle;
static unsigned char *data=MAP_FAILED;
static unsigned long long readbytes,reads;
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void cleanup(void){
 if(data!=MAP_FAILED){munmap(data,CAPACITY);data=MAP_FAILED;}
 if(shared>=0){close(shared);shared=-1;}
 if(ion>=0){if(handle&&ioctl(ion,0xc0044901,&handle))perror("ION free");close(ion);ion=-1;}
 free(entries);entries=NULL;
}
static int find(const char*path){
 if(!strcmp(path,"/"))return -2;
 if(path[0]!='/')return -1;
 int lo=0,hi=(int)count-1;
 while(lo<=hi){int m=lo+(hi-lo)/2,c=strcmp(path+1,entries[m].path);if(!c)return m;if(c<0)hi=m-1;else lo=m+1;}
 return -1;
}
static void attributes(int i,struct stat*st){
 memset(st,0,sizeof(*st));st->st_uid=0;st->st_gid=0;st->st_blksize=4096;st->st_ino=i+3;
 if(i==-2){st->st_mode=S_IFDIR|0555;st->st_nlink=2;return;}
 st->st_mode=entries[i].mode;st->st_nlink=S_ISDIR(st->st_mode)?2:1;st->st_size=entries[i].size;st->st_blocks=(st->st_size+511)/512;
}
static int fs_getattr(const char*p,struct stat*s){int i=find(p);if(i==-1)return -ENOENT;attributes(i,s);return 0;}
static int fs_readdir(const char*p,void*b,fuse_fill_dir_t fill,off_t off,struct fuse_file_info*fi){
 (void)off;(void)fi;int i=find(p);if(i==-1)return -ENOENT;if(i>=0&&!S_ISDIR(entries[i].mode))return -ENOTDIR;
 fill(b,".",NULL,0);fill(b,"..",NULL,0);
 size_t n=i==-2?0:strlen(p+1);
 for(unsigned j=0;j<count;j++){
  const char*q=entries[j].path;
  if(n){if(strncmp(q,p+1,n)||q[n]!='/')continue;q+=n+1;}
  if(!*q||strchr(q,'/'))continue;
  struct stat st;attributes(j,&st);if(fill(b,q,&st,0))break;
 }
 return 0;
}
static int fs_open(const char*p,struct fuse_file_info*fi){
 int i=find(p);if(i==-1)return -ENOENT;if(i==-2||S_ISDIR(entries[i].mode))return -EISDIR;
 if((fi->flags&O_ACCMODE)!=O_RDONLY)return -EROFS;
 fi->fh=(uint64_t)i;fi->keep_cache=1;return 0;
}
static int fs_read(const char*p,char*b,size_t n,off_t off,struct fuse_file_info*fi){
 (void)p;unsigned i=fi->fh;if(i>=count||off<0)return -EINVAL;
 if((uint64_t)off>=entries[i].size)return 0;
 if(n>entries[i].size-(uint32_t)off)n=entries[i].size-(uint32_t)off;
 memcpy(b,data+entries[i].offset+off,n);reads++;readbytes+=n;return (int)n;
}
static int fs_statfs(const char*p,struct statvfs*s){
 (void)p;memset(s,0,sizeof(*s));s->f_bsize=s->f_frsize=4096;s->f_blocks=(packsize+4095)/4096;s->f_files=count+1;s->f_namemax=255;s->f_flag=ST_RDONLY|ST_NOSUID;return 0;
}
static int fs_access(const char*p,int mode){int i=find(p);if(i==-1)return -ENOENT;if(mode&W_OK)return -EROFS;return 0;}
static void*fs_init(struct fuse_conn_info*c){
 c->max_write=65536;printf("ION_CACHE_READY files=%u bytes=%u protocol=%u.%u\n",count,packsize,c->proto_major,c->proto_minor);return NULL;
}
static void fs_destroy(void*p){(void)p;printf("ION_CACHE_STOP reads=%llu bytes=%llu\n",reads,readbytes);}
static struct fuse_operations ops={.getattr=fs_getattr,.readdir=fs_readdir,.open=fs_open,.read=fs_read,.statfs=fs_statfs,.access=fs_access,.init=fs_init,.destroy=fs_destroy};
int main(int argc,char**argv){
 setvbuf(stdout,NULL,_IONBF,0);
 if(argc!=3){fprintf(stderr,"Usage: ion-cache verified-pack mountpoint\n");return 2;}
 int input=open(argv[1],O_RDONLY|O_CLOEXEC);struct stat st;struct header h;
 if(input<0||fstat(input,&st)||read(input,&h,sizeof(h))!=sizeof(h)){perror("pack");return 2;}
 if(memcmp(h.magic,"DOVION01",8)||h.count>MAX_FILES||!h.count||h.size!=st.st_size||h.size>CAPACITY||h.size<sizeof(h)+h.count*sizeof(struct entry)){fprintf(stderr,"Invalid pack header\n");close(input);return 2;}
 count=h.count;packsize=h.size;entries=calloc(count,sizeof(*entries));if(!entries){close(input);return 3;}
 if(read(input,entries,count*sizeof(*entries))!=(ssize_t)(count*sizeof(*entries))){puts("Invalid index");close(input);cleanup();return 3;}
 for(unsigned i=0;i<count;i++){
  struct entry*e=&entries[i];
  if(!memchr(e->path,0,sizeof(e->path))||!e->path[0]||e->path[0]=='/'||strstr(e->path,"..")||(i&&strcmp(entries[i-1].path,e->path)>=0)||(!S_ISDIR(e->mode)&&!S_ISREG(e->mode))||(uint64_t)e->offset+e->size>packsize||(S_ISREG(e->mode)&&e->offset<sizeof(h)+count*sizeof(*e))){puts("Invalid index entry");close(input);cleanup();return 3;}
  e->mode&=~0222;
 }
 ion=open("/dev/ion",O_RDWR|O_CLOEXEC);if(ion<0){perror("ion");close(input);cleanup();return 4;}
 struct allocation a={CAPACITY,4096,0x80,0xf0000000,0};
 if(ioctl(ion,0xc0144900,&a)){perror("ION allocation");close(input);cleanup();return 4;}handle=a.handle;
 struct physical p={handle,0,0,{0,0}};struct share sh={handle,-1};
 if(ioctl(ion,0xc00c4908,&p)||p.len!=CAPACITY||p.address<0x10100000||(uint64_t)p.address+p.len>0x18000000){puts("ION allocation outside permitted pool");close(input);cleanup();return 4;}
 if(ioctl(ion,0xc0084904,&sh)){perror("ION share");close(input);cleanup();return 4;}shared=sh.fd;
 data=mmap(NULL,CAPACITY,PROT_READ|PROT_WRITE,MAP_SHARED,shared,0);
 if(data==MAP_FAILED){perror("ION map");close(input);cleanup();return 4;}
 lseek(input,0,SEEK_SET);size_t done=0;double started=now();
 while(done<packsize){size_t n=packsize-done;if(n>262144)n=262144;ssize_t r=read(input,data+done,n);if(r<=0){perror("pack load");close(input);cleanup();return 5;}done+=r;}
 posix_fadvise(input,0,0,POSIX_FADV_DONTNEED);close(input);
 if(memcmp(data,&h,sizeof(h))){puts("ION header mismatch");cleanup();return 5;}
 printf("ION_CACHE_OWNED physical=%08x allocation=%u loaded=%u seconds=%.3f\n",p.address,p.len,packsize,now()-started);
 char*args[]={argv[0],argv[2],"-f","-s","-o","ro,default_permissions,allow_other,fsname=doveos-ion-cache,max_read=65536,attr_timeout=3600,entry_timeout=3600",NULL};
 int result=fuse_main(6,args,&ops,NULL);cleanup();puts("ION_CACHE_RELEASED");return result;
}
