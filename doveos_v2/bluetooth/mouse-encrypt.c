/* Restore encryption on one owned BLE connection using its saved BlueZ LTK.
 * No key values are printed or passed as process arguments. */
#define _POSIX_C_SOURCE 200809L
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
static void wipe(void *data,size_t n){volatile unsigned char *p=data;while(n--)*p++=0;}
int main(int argc,char **argv){
 if(argc!=2||strlen(argv[1])!=17)return 2;
 bdaddr_t remote;unsigned bytes[6];char tail;
 if(sscanf(argv[1],"%2x:%2x:%2x:%2x:%2x:%2x%c",&bytes[5],&bytes[4],&bytes[3],&bytes[2],&bytes[1],&bytes[0],&tail)!=6)return 2;
 for(unsigned i=0;i<6;i++)remote.b[i]=(uint8_t)bytes[i];
 struct rlimit limit={0,0};setrlimit(RLIMIT_CORE,&limit);prctl(PR_SET_DUMPABLE,0);
 int fd=socket(AF_BLUETOOTH,SOCK_RAW|SOCK_CLOEXEC,BTPROTO_HCI);if(fd<0)return 3;
 struct {uint16_t id,count;struct hci_conn_info info[32];} list={.id=0,.count=32};
 if(ioctl(fd,HCIGETCONNLIST,&list)){close(fd);return 3;}
 unsigned handle=0xffff,link_mode=0;
 for(unsigned i=0;i<list.count&&i<32;i++)if(!memcmp(&list.info[i].bdaddr,&remote,6)){handle=list.info[i].handle;link_mode=list.info[i].link_mode;break;}
 if(handle==0xffff){puts("MOUSE_ENCRYPTION_NO_CONNECTION");close(fd);return 4;}
 if(link_mode&HCI_LM_ENCRYPT){puts("MOUSE_ENCRYPTION_ALREADY_ACTIVE");close(fd);return 0;}
 struct hci_dev_info dev={.dev_id=0};if(ioctl(fd,HCIGETDEVINFO,&dev)){close(fd);return 3;}
 char path[180];snprintf(path,sizeof path,"/var/lib/bluetooth/%02X:%02X:%02X:%02X:%02X:%02X/%.17s/info",dev.bdaddr.b[5],dev.bdaddr.b[4],dev.bdaddr.b[3],dev.bdaddr.b[2],dev.bdaddr.b[1],dev.bdaddr.b[0],argv[1]);
 GKeyFile *file=g_key_file_new();
 if(!g_key_file_load_from_file(file,path,G_KEY_FILE_NONE,NULL)){g_key_file_unref(file);close(fd);puts("MOUSE_ENCRYPTION_SAVED_KEY_UNAVAILABLE");return 5;}
 gchar *key=g_key_file_get_string(file,"LongTermKey","Key",NULL);
 GError *error=NULL;guint64 random=g_key_file_get_uint64(file,"LongTermKey","Rand",&error);
 gint diversifier=g_key_file_get_integer(file,"LongTermKey","EDiv",NULL);
 gint size=g_key_file_get_integer(file,"LongTermKey","EncSize",NULL);
 if(error||!key||strlen(key)!=32||size<7||size>16||diversifier<0||diversifier>65535){g_clear_error(&error);if(key){wipe(key,strlen(key));g_free(key);}g_key_file_unref(file);close(fd);return 5;}
 uint8_t packet[4+LE_START_ENCRYPTION_CP_SIZE]={HCI_COMMAND_PKT,0x19,0x20,LE_START_ENCRYPTION_CP_SIZE};
 packet[4]=handle&255;packet[5]=handle>>8;
 for(unsigned i=0;i<8;i++)packet[6+i]=(random>>(i*8))&255;
 packet[14]=diversifier&255;packet[15]=(unsigned)diversifier>>8;
 for(unsigned i=0;i<16;i++){
  int hi=g_ascii_xdigit_value(key[i*2]),lo=g_ascii_xdigit_value(key[i*2+1]);
  if(hi<0||lo<0){wipe(packet,sizeof packet);wipe(key,strlen(key));g_free(key);g_key_file_unref(file);close(fd);return 5;}
  packet[16+i]=(hi<<4)|lo;
 }
 wipe(key,strlen(key));g_free(key);g_key_file_unref(file);
 struct sockaddr_hci address={.hci_family=AF_BLUETOOTH,.hci_dev=0,.hci_channel=HCI_CHANNEL_RAW};
 struct hci_filter filter;hci_filter_clear(&filter);hci_filter_set_ptype(HCI_EVENT_PKT,&filter);hci_filter_set_event(EVT_ENCRYPT_CHANGE,&filter);hci_filter_set_event(EVT_CMD_STATUS,&filter);
 if(bind(fd,(struct sockaddr*)&address,sizeof address)||setsockopt(fd,SOL_HCI,HCI_FILTER,&filter,sizeof filter)){wipe(packet,sizeof packet);close(fd);return 3;}
 struct timeval timeout={1,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);
 ssize_t sent=write(fd,packet,sizeof packet);wipe(packet,sizeof packet);if(sent!=(ssize_t)sizeof packet){close(fd);return 3;}
 gint64 deadline=g_get_monotonic_time()+8000000;
 while(g_get_monotonic_time()<deadline){
  uint8_t event[64];ssize_t n=read(fd,event,sizeof event);if(n<3||event[0]!=HCI_EVENT_PKT)continue;
  if(event[1]==EVT_ENCRYPT_CHANGE&&n>=7&&(unsigned)(event[4]|(event[5]<<8))==handle){
   printf("MOUSE_ENCRYPTION status=0x%02x enabled=%u\n",event[3],event[6]);close(fd);return !event[3]&&event[6]?0:6;
  }
  if(event[1]==EVT_CMD_STATUS&&n>=7&&event[5]==0x19&&event[6]==0x20&&event[3]){
   printf("MOUSE_ENCRYPTION_COMMAND status=0x%02x\n",event[3]);close(fd);return 6;
  }
 }
 puts("MOUSE_ENCRYPTION_TIMEOUT");close(fd);return 6;
}
