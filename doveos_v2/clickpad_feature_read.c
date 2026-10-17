#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>
int main(void){unsigned bus=0,dev=0;FILE*f=fopen("/sys/bus/usb/devices/1-1.3/busnum","r");if(!f)return 1;if(fscanf(f,"%u",&bus)!=1)return 1;fclose(f);f=fopen("/sys/bus/usb/devices/1-1.3/devnum","r");if(!f)return 1;if(fscanf(f,"%u",&dev)!=1)return 1;fclose(f);char path[100];snprintf(path,sizeof path,"/dev/bus/usb/%03u/%03u",bus,dev);int fd=open(path,O_RDWR);if(fd<0){perror("usb open");return 1;}unsigned ids[]={2,3,5};unsigned lengths[]={2,3,3};for(unsigned i=0;i<3;i++){unsigned char b[16]={0};struct usbdevfs_ctrltransfer q={.bRequestType=0xa1,.bRequest=1,.wValue=0x300|ids[i],.wIndex=1,.wLength=lengths[i],.timeout=1000,.data=b};int n=ioctl(fd,USBDEVFS_CONTROL,&q);printf("GET_FEATURE id=%u result=%d",ids[i],n);if(n<0)printf(" errno=%d %s",errno,strerror(errno));else for(int j=0;j<n;j++)printf(" %02x",b[j]);puts("");}close(fd);return 0;}
