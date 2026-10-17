#define _POSIX_C_SOURCE 200809L
/* One ready-screen BL is replaced in process memory, never in the ELF file.
 * The exec-stop occurs before HP threads exist. Restore after desktop readiness
 * or 90 monotonic seconds. All other HP screen cases retain their stock code. */
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <limits.h>
#define EXE "/usr/local/bin/IMS_Modules/wfd_dfb"
#define ADDRESS 0xb2618UL
#define STOCK 0xeb000a6fU
#define NOP 0xe1a00000U
static int word(pid_t p,uint32_t *v){errno=0;long x=ptrace(PTRACE_PEEKTEXT,p,(void*)ADDRESS,0);if(x==-1&&errno)return -1;*v=(uint32_t)x;return 0;}
static int change(pid_t p,uint32_t from,uint32_t to){uint32_t v;if(word(p,&v)||v!=from)return -1;if(ptrace(PTRACE_POKETEXT,p,(void*)ADDRESS,(void*)(unsigned long)to))return -1;if(word(p,&v)||v!=to){ptrace(PTRACE_POKETEXT,p,(void*)ADDRESS,(void*)(unsigned long)from);return -1;}return 0;}
static int inspect(pid_t p,int restore){char path[80],exe[PATH_MAX];snprintf(path,sizeof path,"/proc/%d/exe",p);ssize_t n=readlink(path,exe,sizeof exe-1);if(n<0)return 1;exe[n]=0;if(strcmp(exe,EXE))return 1;
 if(ptrace(PTRACE_ATTACH,p,0,0))return 1;
 int s=0,rc=1;uint32_t v=0;
 if(waitpid(p,&s,0)==p&&WIFSTOPPED(s)&&!word(p,&v)){printf("FOREGROUND_WORD=%08x\n",v);if(restore)rc=v==STOCK?0:change(p,NOP,STOCK);else rc=v==STOCK?0:1;}
 if(ptrace(PTRACE_DETACH,p,0,0))rc=1;
 return rc;}
int main(int argc,char **argv){setvbuf(stdout,0,_IONBF,0);
 if(argc==3&&(!strcmp(argv[1],"verify")||!strcmp(argv[1],"restore"))){char *e;long p=strtol(argv[2],&e,10);if(*e||p<=1||p>INT_MAX)return 2;return inspect((pid_t)p,!strcmp(argv[1],"restore"));}
 if(argc!=2||strcmp(argv[1],"launch"))return 2;
 if(chdir("/usr/local/bin/IMS_Modules"))return 1;
 pid_t p=fork();if(p<0)return 1;if(!p){if(!freopen("/dev/console","w",stdout)||dup2(1,2)<0)_exit(125);if(ptrace(PTRACE_TRACEME,0,0,0))_exit(126);execl(EXE,"./wfd_dfb",(char*)0);_exit(127);}
 int s;int applied=0;if(waitpid(p,&s,0)!=p)return 1;
 if(WIFSTOPPED(s)){applied=change(p,STOCK,NOP)==0;printf("FOREGROUND_TRIAL_APPLIED=%d pid=%d\n",applied,p);if(ptrace(PTRACE_DETACH,p,0,0)){kill(p,SIGCONT);return 1;}}
 else {puts("HP_EXEC_FAILED");return 1;}
 FILE *f=fopen("/tmp/doveos-foreground-child.pid","w");if(f){fprintf(f,"%d\n",p);fclose(f);}
 if(!applied)return 0;
 for(int i=0;i<90;i++){sleep(1);if(kill(p,0))return 0;if(i>=15&&access("/tmp/doveos-splash-desktop-ready",F_OK)==0)break;}
 int rc=inspect(p,1);printf("FOREGROUND_RESTORE_RESULT=%d\n",rc);return rc;}
