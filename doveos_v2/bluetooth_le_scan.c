/* Bounded LE discovery bypassing the unsupported vendor MGMT LE path. */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
struct hci_address{sa_family_t family;unsigned short device,channel;};
static volatile sig_atomic_t stopping;
static int radio;
static void stop(int sig){(void)sig;stopping=1;}
static double mono(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static struct{unsigned char address[6],type;char name[90];} seen[128];
static int seen_count;
static void advertising(const unsigned char *b,int n){
 if(n<5||b[1]!=0x3e||b[3]!=2)return;
 int p=5;
 for(unsigned report=0;report<b[4]&&p+9<n;report++){
  int len=b[p+8];if(p+10+len>n)break;
  char name[90]="Unknown device";
  for(int i=0;i<len;){int z=b[p+9+i];if(!z||i+z+1>len)break;
   if((b[p+10+i]==8||b[p+10+i]==9)&&z>1){int count=z-1;if(count>89)count=89;
    for(int j=0;j<count;j++){unsigned char c=b[p+11+i+j];name[j]=(c>=32&&c!=127)?(char)c:' ';}name[count]=0;
    if(b[p+10+i]==9)break;
   }i+=z+1;
  }
  int at=0;while(at<seen_count&&(memcmp(seen[at].address,b+p+2,6)||seen[at].type!=b[p+1]))at++;
  int fresh=at==seen_count;if(fresh&&seen_count>=128){p+=10+len;continue;}
  if(fresh){memcpy(seen[at].address,b+p+2,6);seen[at].type=b[p+1];seen_count++;}
  if(fresh||(strcmp(name,"Unknown device")&&strcmp(name,seen[at].name))){
   snprintf(seen[at].name,sizeof seen[at].name,"%s",name);
   printf("%02X:%02X:%02X:%02X:%02X:%02X %s\n",b[p+7],b[p+6],b[p+5],b[p+4],b[p+3],b[p+2],name);
  }
  p+=10+len;
 }
}
static int send_command(unsigned op,const unsigned char *data,unsigned count){
 unsigned char cmd[16]={1,(unsigned char)op,(unsigned char)(op>>8),(unsigned char)count};
 if(count>12)return -1;
 memcpy(cmd+4,data,count);
 return send(radio,cmd,count+4,0)==(ssize_t)(count+4)?0:-1;
}
static int command(unsigned op,const unsigned char *data,unsigned count){
 if(send_command(op,data,count))return -1;
 double end=mono()+2;
 while(mono()<end){unsigned char b[260];int n=recv(radio,b,sizeof b,0);if(n<3)continue;
  advertising(b,n);
  if(n>=7&&b[1]==0x0e&&(((unsigned)b[5]<<8)|b[4])==op){fprintf(stderr,"LE_COMMAND=%04x status=%02x\n",op,b[6]);return b[6]? -1:0;}
 }return -1;
}
int main(void){
 sigset_t m;sigemptyset(&m);sigprocmask(SIG_SETMASK,&m,NULL);
 signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGALRM,stop);signal(SIGPIPE,SIG_IGN);alarm(14);setvbuf(stdout,NULL,_IOLBF,0);
 radio=socket(31,SOCK_RAW,1);struct hci_address a={31,0,0};
 if(radio<0||bind(radio,(struct sockaddr*)&a,sizeof a)){perror("LE socket");return 1;}
 struct{uint32_t type,events[2];uint16_t opcode;} filter={1u<<4,{(1u<<14)|(1u<<15),1u<<(0x3e - 32)},0};
 struct timeval wait={0,250000};
 if(setsockopt(radio,0,2,&filter,sizeof filter)||setsockopt(radio,SOL_SOCKET,SO_RCVTIMEO,&wait,sizeof wait))return 2;
 unsigned char params[]={1,0x60,0,0x30,0,0,0},enable[]={1,0},disable[]={0,0};
 if(command(0x200b,params,sizeof params))return 3;
 int pipefd[2];if(pipe(pipefd))return 4;pid_t guard=fork();if(guard<0)return 4;
 if(!guard){close(pipefd[1]);signal(SIGTERM,SIG_IGN);signal(SIGINT,SIG_IGN);signal(SIGALRM,SIG_IGN);
  struct pollfd p={pipefd[0],POLLIN,0};unsigned char done=0;
  if(poll(&p,1,16000)<=0||read(pipefd[0],&done,1)!=1||done!=1)send_command(0x200c,disable,sizeof disable);
  close(pipefd[0]);close(radio);_exit(0);
 }
 close(pipefd[0]);int ok=command(0x200c,enable,sizeof enable)==0;
 if(ok){double end=mono()+8;while(!stopping&&mono()<end){unsigned char b[260];int n=recv(radio,b,sizeof b,0);if(n>=3)advertising(b,n);}}
 int stopped=command(0x200c,disable,sizeof disable)==0;
 if(stopped){unsigned char done=1;ssize_t sent=write(pipefd[1],&done,1);(void)sent;}
 close(pipefd[1]);waitpid(guard,NULL,0);close(radio);
 fprintf(stderr,"LE_FOUND_UNIQUE=%d LE_STOPPED=%d\n",seen_count,stopped);return ok&&stopped?0:5;
}
