#define _GNU_SOURCE
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
int main(int argc,char **argv){
 if(argc<3)return 2;
 if(chroot(argv[1])||chdir("/root")){perror("light app root");return 3;}
 sigset_t mask;sigemptyset(&mask);sigprocmask(SIG_SETMASK,&mask,NULL);signal(SIGCHLD,SIG_DFL);
 setenv("HOME","/root",1);setenv("DISPLAY",":7",1);setenv("LANG","C",1);
 setenv("GDK_BACKEND","x11",1);setenv("GSETTINGS_BACKEND","memory",1);setenv("NO_AT_BRIDGE","1",1);
 setenv("LD_LIBRARY_PATH","/usr/lib/arm-linux-gnueabihf:/lib/arm-linux-gnueabihf",1);
 unsetenv("GTK_PATH");setenv("GDK_PIXBUF_MODULE_FILE","/tmp/doveos-light-loaders.cache",1);unsetenv("GDK_PIXBUF_MODULEDIR");unsetenv("GTK_THEME");
 execv(argv[2],argv+2);perror("light app");return 4;
}
