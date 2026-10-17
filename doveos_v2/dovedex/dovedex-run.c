/* SPDX-License-Identifier: MIT
 * Bound one owned process group. Never signal the desktop/input services. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
static volatile sig_atomic_t interrupted;
static void signal_received(int s){interrupted=s;}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
int main(int argc,char **argv){
    if(argc>=3&&!strcmp(argv[1],"--exec")){
        sigset_t clear;sigemptyset(&clear);sigprocmask(SIG_SETMASK,&clear,NULL);
        signal(SIGCHLD,SIG_DFL);signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);signal(SIGHUP,SIG_DFL);
        execvp(argv[2],argv+2);perror("exec");return 127;
    }
    if(argc<3){fputs("Usage: dovedex-run seconds command [args]\n",stderr);return 2;}
    char *end;long seconds=strtol(argv[1],&end,10);
    if(*end||seconds<1||seconds>3600)return 2;
    sigset_t clear;sigemptyset(&clear);sigprocmask(SIG_SETMASK,&clear,NULL);
    signal(SIGCHLD,SIG_DFL);signal(SIGTERM,signal_received);signal(SIGINT,signal_received);signal(SIGHUP,signal_received);
    pid_t p=fork();if(p<0){perror("fork");return 3;}
    if(!p){
        setpgid(0,0);
        signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);signal(SIGHUP,SIG_DFL);
        execvp(argv[2],argv+2);perror("exec");_exit(127);
    }
    setpgid(p,p);
    double deadline=now()+seconds;int status;struct timespec pause={0,20000000};
    while(now()<deadline&&!interrupted){
        pid_t n=waitpid(p,&status,WNOHANG);
        if(n==p)return WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);
        if(n<0&&errno!=EINTR){perror("waitpid");return 4;}
        nanosleep(&pause,NULL);
    }
    kill(-p,SIGTERM);
    deadline=now()+1;
    while(now()<deadline){
        if(waitpid(p,&status,WNOHANG)==p)return interrupted?128+interrupted:124;
        nanosleep(&pause,NULL);
    }
    kill(-p,SIGKILL);
    while(waitpid(p,&status,0)<0&&errno==EINTR){}
    return interrupted?128+interrupted:124;
}
