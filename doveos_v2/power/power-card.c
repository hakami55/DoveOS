#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "font.h"
static uint32_t *pixels;
static void text(int y,const char *s,uint32_t colour,int scale){
 int x=(1280-(int)strlen(s)*12*scale)/2;
 for(;*s;s++,x+=12*scale){unsigned c=(unsigned char)*s;if(c<32||c>126)continue;
  for(int j=0;j<20;j++)for(int i=0;i<12;i++)if(font[c-32][j]&(1u<<i))
   for(int v=0;v<scale;v++)for(int u=0;u<scale;u++){
    int xx=x+i*scale+u,yy=y+j*scale+v;if(xx>=0&&xx<1280&&yy>=0&&yy<720)pixels[yy*1280+xx]=colour;
   }
 }
}
int main(int argc,char **argv){
 if(argc!=2)return 2;
 int safe=!strcmp(argv[1],"safe"),restart=!strcmp(argv[1],"restarting"),fail=!strcmp(argv[1],"failed"),probe=!strcmp(argv[1],"--probe");
 if(!safe&&!restart&&!fail&&!probe)return 2;
 if(safe&&access("/tmp/doveos-system-usb-unmounted",R_OK))return 6;
 int fd=open("/dev/fb0",(probe?O_RDONLY:O_RDWR)|O_CLOEXEC);struct fb_var_screeninfo v={0};struct fb_fix_screeninfo f={0};
 if(fd<0||ioctl(fd,FBIOGET_VSCREENINFO,&v)||ioctl(fd,FBIOGET_FSCREENINFO,&f))return 3;
 if(v.xres!=1280||v.yres!=720||v.bits_per_pixel!=32||f.line_length!=5120||f.smem_len<1280*720*4||v.xoffset||v.yoffset||v.red.offset!=16||v.green.offset!=8||v.blue.offset!=0||v.transp.offset!=24)return 4;
 if(probe){puts("POWER_CARD_FRAMEBUFFER_READY");close(fd);return 0;}
 pixels=mmap(NULL,1280*720*4,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);if(pixels==MAP_FAILED)return 5;
 for(int i=0;i<1280*720;i++)pixels[i]=0xff000000;
 text(180,"DoveOS",0xffdddddd,2);
 text(295,safe?"It is now safe to remove":restart?"Restarting the Lap Dock...":"USB could not be released.",safe?0xffffb75d:0xffeeeeee,2);
 text(345,safe?"your system USB.":restart?"Keep USB and power connected.":"DO NOT REMOVE THE USB.",safe?0xffffb75d:0xffeeeeee,2);
 text(445,safe?"Keep power connected while removing the USB.":"",0xffbbbbbb,1);
 text(480,safe?"Reinsert the USB and restart the dock to use DoveOS again.":fail?"Leave USB connected. Restart DoveOS before trying again.":"",0xffbbbbbb,1);
 __sync_synchronize();munmap(pixels,1280*720*4);close(fd);return 0;
}
