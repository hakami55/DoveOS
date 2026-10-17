#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#define FUSE_USE_VERSION 26
#include <fuse.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static int input;static struct stat original;static int direct;
static int attr(const char *p,struct stat *s){
 memset(s,0,sizeof(*s));s->st_uid=0;s->st_gid=0;s->st_blksize=4096;
 if(!strcmp(p,"/")){s->st_mode=S_IFDIR|0555;s->st_nlink=2;return 0;}
 if(strcmp(p,"/image"))return -ENOENT;
 s->st_mode=S_IFREG|0444;s->st_nlink=1;s->st_size=original.st_size;s->st_blocks=(s->st_size+511)/512;return 0;
}
static int listing(const char*p,void*b,fuse_fill_dir_t f,off_t o,struct fuse_file_info*i){(void)o;(void)i;if(strcmp(p,"/"))return -ENOENT;f(b,".",NULL,0);f(b,"..",NULL,0);f(b,"image",NULL,0);return 0;}
static int opening(const char*p,struct fuse_file_info*i){if(strcmp(p,"/image"))return -ENOENT;if((i->flags&O_ACCMODE)!=O_RDONLY)return -EROFS;i->direct_io=1;return 0;}
static int reading(const char*p,char*b,size_t n,off_t o,struct fuse_file_info*i){
 (void)i;if(strcmp(p,"/image"))return -ENOENT;if(o<0)return -EINVAL;if(o>=original.st_size)return 0;
 if(n>(size_t)(original.st_size-o))n=original.st_size-o;
 off_t start=o&~(off_t)4095;size_t length=((o-start+n+4095)/4096)*4096;void *aligned=NULL;
 if(posix_memalign(&aligned,4096,length))return -ENOMEM;
 ssize_t got=pread(input,aligned,length,start);int result;
 if(got<0)result=-errno;else if(got<=o-start)result=0;else{size_t have=got-(o-start);if(have>n)have=n;memcpy(b,(char*)aligned+o-start,have);result=have;}
 if(!direct)posix_fadvise(input,start,length,POSIX_FADV_DONTNEED);
 free(aligned);return result;
}
static struct fuse_operations ops={.getattr=attr,.readdir=listing,.open=opening,.read=reading};
int main(int argc,char **argv){
 if(argc!=3)return 2;input=open(argv[1],O_RDONLY|O_CLOEXEC|O_DIRECT);direct=input>=0;
 if(input<0)input=open(argv[1],O_RDONLY|O_CLOEXEC);
 if(input<0||fstat(input,&original)||!S_ISREG(original.st_mode))return 3;
 void *test=NULL;if(posix_memalign(&test,4096,4096))return 4;
 if(pread(input,test,4096,0)!=4096){perror("read image");return 4;}free(test);
 printf("READ_ONLY_IMAGE direct=%d size=%lld\n",direct,(long long)original.st_size);fflush(stdout);
 char *args[]={argv[0],argv[2],"-f","-s","-o","ro,allow_other,default_permissions,fsname=doveos-browser-direct,max_read=131072,attr_timeout=3600,entry_timeout=3600",NULL};
 int result=fuse_main(6,args,&ops,NULL);close(input);return result;
}
