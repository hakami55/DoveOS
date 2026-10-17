#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <unistd.h>
#include <string.h>
int main(int argc,char **argv){
 sigset_t mask;sigemptyset(&mask);sigprocmask(SIG_SETMASK,&mask,0);
 signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);signal(SIGPIPE,SIG_DFL);
 if(argc<3||strcmp(argv[1],"--exec"))return 2;
 execvp(argv[2],argv+2);return 127;
}
