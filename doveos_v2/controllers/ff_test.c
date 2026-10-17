#define _GNU_SOURCE
#include <linux/input.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
int main(void) {
 int fd=-1;
 for(int i=0;i<128;i++) {
  char path[64],name[128]={0};snprintf(path,sizeof path,"/dev/input/event%d",i);
  int one=open(path,O_RDWR|O_CLOEXEC);if(one<0)continue;
  ioctl(one,EVIOCGNAME(sizeof name),name);
  if(!strcmp(name,"Microsoft Xbox One USB (DoveOS)")){fd=one;printf("MATCH %s\n",path);break;}close(one);
 }
 if(fd<0){puts("No DoveOS Xbox bridge");return 1;}
 struct ff_effect effect;memset(&effect,0,sizeof effect);effect.type=FF_RUMBLE;effect.id=-1;effect.replay.length=1000;
 effect.u.rumble.strong_magnitude=20000;effect.u.rumble.weak_magnitude=20000;
 if(ioctl(fd,EVIOCSFF,&effect)<0){perror("upload effect");close(fd);return 1;}
 struct input_event e;memset(&e,0,sizeof e);e.type=EV_FF;e.code=effect.id;e.value=1;
 if(write(fd,&e,sizeof e)!=(ssize_t)sizeof e){perror("start effect");close(fd);return 1;}
 puts("RUMBLE_TEST_STARTED");usleep(1500000);
 e.value=0;if(write(fd,&e,sizeof e)!=(ssize_t)sizeof e)perror("stop effect");
 ioctl(fd,EVIOCRMFF,effect.id);close(fd);puts("RUMBLE_TEST_FINISHED");return 0;
}
