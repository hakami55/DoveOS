#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

/* Clear the old stock overlay, never change its geometry or RPC ownership. */
int main(int argc,char **argv){
 int probe=argc==2&&!strcmp(argv[1],"--probe");
 if(!probe&&(argc!=2||strcmp(argv[1],"--clear")))return 2;
 int fd=open("/dev/fb0",(probe?O_RDONLY:O_RDWR)|O_CLOEXEC);
 struct fb_var_screeninfo v={0};struct fb_fix_screeninfo f={0};
 if(fd<0||ioctl(fd,FBIOGET_VSCREENINFO,&v)||ioctl(fd,FBIOGET_FSCREENINFO,&f))return 3;
 if(v.xres!=1280||v.yres!=720||v.bits_per_pixel!=32||f.line_length!=5120||
    f.smem_len<1280*720*4||v.xoffset||v.yoffset||v.red.offset!=16||
    v.green.offset!=8||v.blue.offset!=0||v.transp.offset!=24||
    v.red.length!=8||v.green.length!=8||v.blue.length!=8||v.transp.length!=8){
  puts("TRANSITION_FRAMEBUFFER_UNSUPPORTED");close(fd);return 4;
 }
 if(probe){puts("TRANSITION_FRAMEBUFFER_VERIFIED 1280x720 ARGB32");close(fd);return 0;}
 size_t size=1280*720*4;
 uint32_t *map=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
 if(map==MAP_FAILED){close(fd);return 5;}
 /* Transparent black lets the video plane remain visible during playback. */
 memset(map,0,size);__sync_synchronize();
 munmap(map,size);close(fd);puts("TRANSITION_STALE_BACKGROUND_CLEARED");return 0;
}
