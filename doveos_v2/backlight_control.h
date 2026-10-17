/* T0053-only backlight service. Included by the sole ITE diagnostic owner. */
#include <poll.h>
#include <sys/wait.h>
static int backlight_ready,backlight_base,backlight_forced,backlight_guard=-1,backlight_actual=-1;
static int backlight_level=-1;
static pid_t backlight_guard_pid;
static int pwm_write(unsigned value){
 if(!backlight_ready||value>100)return -1;
 /* One audited volatile register: PWM DCR0. Never write flash, GPIO, or RAM. */
 unsigned char req[17]={0x5a,6,2,0x18,0xf0,1,0};req[6]=(unsigned char)value;
 struct usbdevfs_ctrltransfer q={.bRequestType=0x20,.bRequest=9,.wValue=0x35a,.wLength=17,.timeout=1000,.data=req};
 if(ioctl(usbfd,USBDEVFS_CONTROL,&q)!=17)return -1;
 unsigned char actual;if(read_fixed(0xf01802,1,&actual))return -1;
 backlight_actual=actual;return actual==value?0:-1;
}
static void backlight_stop_guard(void){
 if(backlight_guard>=0){char stop=0;ssize_t sent=write(backlight_guard,&stop,1);(void)sent;close(backlight_guard);backlight_guard=-1;waitpid(backlight_guard_pid,NULL,0);}
}
static void backlight_restore(void){
 backlight_stop_guard();
 if(backlight_forced)pwm_write((unsigned)backlight_base);
 backlight_forced=0;
 unlink("/tmp/doveos-backlight-state");
}
/* Persist a real Fn brightness-level change, never a temporary dim/off PWM.
   Native keys bypass Linux input and previously left the saved slider stale. */
static int backlight_save_base(void){
 const char *path="/media/usb0/DoveOS-v2/home/display-brightness.txt";
 char temp[180];snprintf(temp,sizeof temp,"%s.%ld.tmp",path,(long)getpid());
 FILE *f=fopen(temp,"w");if(!f)return -1;
 int failed=fprintf(f,"%d\n",backlight_base)<0;
 if(fflush(f)||fsync(fileno(f)))failed=1;
 if(fclose(f))failed=1;
 if(!failed&&!rename(temp,path))return 0;
 unlink(temp);return -1;
}
static int backlight_watchdog(void){
 int p[2];if(pipe(p))return -1;
 pid_t child=fork();if(child<0){close(p[0]);close(p[1]);return -1;}
 if(!child){
  close(p[1]);signal(SIGTERM,SIG_IGN);signal(SIGINT,SIG_IGN);
  struct pollfd fd={p[0],POLLIN,0};char command=1;
  while(poll(&fd,1,3000)>0){if(read(p[0],&command,1)!=1){command=1;break;}if(!command)break;}
  if(command){int result=pwm_write((unsigned)backlight_base);fprintf(stderr,"BACKLIGHT_WATCHDOG_RESTORE duty=%d result=%d\n",backlight_base,result);}
  close(p[0]);close(usbfd);_exit(0);
 }
 close(p[0]);backlight_guard=p[1];backlight_guard_pid=child;return 0;
}
static void backlight_init(void){
 const unsigned char code[]={0xfc,0x00,0x46,0x10,0x00,0x80,0x58,0x10,0x86,0x2a,0xa6,0x88,0x96,0x00,0xe6,0x42,0xe9,0x03,0x84,0x41,0xae,0x88,0x46,0x10,0x00,0x80,0x58,0x10,0x86,0x2a,0xa6,0x88,0x96,0x40,0xe6,0x2b,0xe9,0x02,0x84,0x0a,0x84,0x2b,0x96,0x00,0x42,0x01,0x04,0x73,0x44,0x11,0x88,0xe8,0x98,0x01,0xa6,0x40,0x46,0x00,0x0f,0x01,0x58,0x00,0x08,0x02,0xae,0x40};
 unsigned char live[sizeof code],state[3],pwm;
 for(unsigned i=0;i<sizeof live;i+=10){unsigned n=sizeof live-i;if(n>10)n=10;if(read_fixed(0xe3ba+i,n,live+i))return;}
 if(memcmp(live,code,sizeof live)||read_fixed(0x80628,3,state)||read_fixed(0xf01802,1,&pwm))return;
 if(state[0]>10||state[1]>10||state[2]>1||pwm>100)return;
 backlight_base=pwm?pwm:40;backlight_actual=pwm;backlight_level=state[1];backlight_ready=1;signal(SIGPIPE,SIG_IGN);puts("T0053_BACKLIGHT_PWM0_VERIFIED");
}
static void backlight_tick(int refresh){
 static int saved_previous=-1;static double next=0,settle_until=0,verify_after=0;
 if(!backlight_ready||now()<next)return;
 next=now()+0.2;
 /* Choose the requested state before sampling the hardware. A wake-time
    firmware PWM reset must never become the user's remembered brightness. */
 double lease=0;int mode=0;
 FILE *f=fopen("/tmp/doveos-screen-blank","r");if(f){if(fscanf(f,"%lf",&lease)!=1)lease=0;fclose(f);}
 if(lease>now()&&lease<now()+3)mode=2;
 else {f=fopen("/tmp/doveos-screen-dim","r");if(f){if(fscanf(f,"%lf",&lease)!=1)lease=0;fclose(f);}if(lease>now()&&lease<now()+3)mode=1;}
 int changed=0,waking=backlight_forced&&!mode,entering=!backlight_forced&&mode;
 if(waking)settle_until=now()+2;
 int saved=-1;f=fopen("/media/usb0/DoveOS-v2/home/display-brightness.txt","r");
 if(f){if(fscanf(f,"%d",&saved)!=1)saved=-1;fclose(f);}
 if(saved>=3&&saved<=100&&saved!=saved_previous){backlight_base=saved;saved_previous=saved;changed=1;}
 if(refresh||entering||waking){
  unsigned char levels[2];
  if(!read_fixed(0x80628,2,levels)&&levels[0]<=10&&levels[1]<=10){
   if(!changed&&!backlight_forced&&!waking&&now()>=settle_until&&levels[0]==levels[1]&&levels[1]!=backlight_level){
    static const int duty[]={3,10,20,30,40,50,60,70,80,90,100};
    backlight_base=duty[levels[1]];changed=1;
    if(!backlight_save_base())saved_previous=backlight_base;
   }
   /* Changes during dim/off or wake are observations, not new preferences. */
   backlight_level=levels[1];
  }
 }
 if(refresh||entering||waking||((mode||now()<settle_until)&&now()>=verify_after)){
  unsigned char current;if(!read_fixed(0xf01802,1,&current))backlight_actual=current;
  verify_after=now()+(now()<settle_until?0.2:0.5);
 }
 if(mode!=backlight_forced||changed){
  /* Direct dim -> off transition: never restore full brightness in between.
     Recreate the watchdog when the preference changes so its fallback agrees. */
  backlight_stop_guard();
  if(mode&&backlight_watchdog())mode=0;
  backlight_forced=mode;
  unsigned target=mode==2?0:mode==1?(unsigned)(backlight_base<10?backlight_base:10):(unsigned)backlight_base;
  if(pwm_write(target))backlight_restore();
 }else{
  unsigned target=mode==2?0:mode==1?(unsigned)(backlight_base<10?backlight_base:10):(unsigned)backlight_base;
  /* Correct a firmware reset instead of adopting it as a preference. */
  if(backlight_actual!=(int)target)pwm_write(target);
 }
 if(backlight_guard>=0){char beat=1;if(write(backlight_guard,&beat,1)!=1){close(backlight_guard);backlight_guard=-1;waitpid(backlight_guard_pid,NULL,0);pwm_write((unsigned)backlight_base);backlight_forced=0;}}
 f=fopen("/tmp/doveos-backlight-state.tmp","w");if(f){fprintf(f,"DOVE_BACKLIGHT1 %.3f %d %d %d\n",now(),backlight_base,backlight_forced,backlight_actual);fclose(f);rename("/tmp/doveos-backlight-state.tmp","/tmp/doveos-backlight-state");}
}
