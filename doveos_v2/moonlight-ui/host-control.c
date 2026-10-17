#include "client.h"
#include "errors.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(int argc,char **argv){
 if(argc!=4)return 2;
 setvbuf(stdout,NULL,_IONBF,0);alarm(20);
 SERVER_DATA server;
 int rc=gs_init(&server,argv[2],0,argv[3],2,true);
 if(rc!=GS_OK){fprintf(stderr,"Host connection failed: %s (%d)\n",gs_error?gs_error:"unknown",rc);return 1;}
 printf("PAIRED=%d\nCURRENT_APP=%d\n",server.paired,server.currentGame);
 printf("CODEC_H264=%d\nCODEC_HEVC=%d\n",
        !!(server.serverInfo.serverCodecModeSupport & SCM_H264),
        !!(server.serverInfo.serverCodecModeSupport & SCM_HEVC));
 if(!strcmp(argv[1],"stop"))rc=gs_quit_app(&server);
 else if(!strcmp(argv[1],"unpair"))rc=gs_unpair(&server);
 else if(!strcmp(argv[1],"check")){
  if(!server.paired)return 0;
  PAPP_LIST apps=NULL;rc=gs_applist(&server,&apps);
  if(rc==GS_OK)for(PAPP_LIST a=apps;a;a=a->next)printf("APP=%d:%s\n",a->id,a->name);
 }else return 2;
 if(rc!=GS_OK){fprintf(stderr,"PC request failed: %s (%d)\n",gs_error?gs_error:"unknown",rc);return 1;}
 return 0;
}
