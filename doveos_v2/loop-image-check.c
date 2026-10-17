/* Read-only identity check for a loop device and its current backing pathname.
 * LOOP_GET_STATUS64 records the original backing device/inode even if a USB
 * re-enumeration leaves the same path pointing to a different file. */
#define _GNU_SOURCE
#include <linux/loop.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
int main(int argc,char **argv){
 if(argc!=3||strncmp(argv[1],"/dev/loop",9))return 2;
 for(const char *p=argv[1]+9;*p;p++)if(*p<'0'||*p>'9')return 2;
 if(!argv[1][9])return 2;
 struct stat target,device;struct loop_info64 info={0};
 if(stat(argv[2],&target)||!S_ISREG(target.st_mode)){perror("BACKING_FILE_STAT");return 2;}
 int fd=open(argv[1],O_RDONLY|O_CLOEXEC);
 if(fd<0||fstat(fd,&device)||!S_ISBLK(device.st_mode)||ioctl(fd,LOOP_GET_STATUS64,&info)){
  perror("LOOP_STATUS");if(fd>=0)close(fd);return 2;
 }
 close(fd);
 if(!(info.lo_flags&LO_FLAGS_READ_ONLY)||info.lo_offset||info.lo_sizelimit){
  puts("LOOP_NOT_READ_ONLY_WHOLE_IMAGE");return 2;
 }
 int match=info.lo_device==(uint64_t)target.st_dev&&info.lo_inode==(uint64_t)target.st_ino;
 char sysdev[96];snprintf(sysdev,sizeof sysdev,"/sys/dev/block/%u:%u",major((dev_t)info.lo_device),minor((dev_t)info.lo_device));
 int present=access(sysdev,F_OK)==0;
 printf("LOOP_IMAGE_%s loop_device=%llu current_device=%llu loop_inode=%llu current_inode=%llu backing_device_present=%d\n",
  match?"MATCH":"STALE",(unsigned long long)info.lo_device,(unsigned long long)target.st_dev,
  (unsigned long long)info.lo_inode,(unsigned long long)target.st_ino,present);
 return match?0:1;
}
