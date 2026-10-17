#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <Limelight.h>
#include "../../moonlight_dock_prototype/gamepad_input.h"
int LiSendMultiControllerEvent(short c,short mask,int buttons,unsigned char lt,unsigned char rt,short lx,short ly,short rx,short ry) {
 (void)c;(void)mask;(void)buttons;(void)lt;(void)rt;(void)lx;(void)ly;(void)rx;(void)ry;return 0;
}
int main(void) {
 struct dock_gamepad g={.fd=-1,.legacy_xy=1};
 struct input_event e={.type=EV_KEY,.code=BTN_X,.value=1};
 assert(gamepad_process(&g,&e) && g.buttons==X_FLAG);
 e.value=0;assert(gamepad_process(&g,&e) && g.buttons==0);
 e.code=BTN_Y;e.value=1;assert(gamepad_process(&g,&e) && g.buttons==Y_FLAG);
 g.legacy_xy=0;g.buttons=0;e.code=BTN_WEST;
 assert(gamepad_process(&g,&e) && g.buttons==X_FLAG);
 e.value=0;gamepad_process(&g,&e);e.code=BTN_NORTH;e.value=1;
 assert(gamepad_process(&g,&e) && g.buttons==Y_FLAG);
 /* Capture the real callback packet on an isolated test machine. */
 int s=socket(AF_UNIX,SOCK_DGRAM|SOCK_NONBLOCK,0);assert(s>=0);
 struct sockaddr_un a={.sun_family=AF_UNIX};
 strcpy(a.sun_path,"/tmp/doveos-xbox-usb-rumble.sock");
 assert(bind(s,(struct sockaddr*)&a,sizeof a)==0);
 gamepad_rumble_available=1;gamepad_rumble(0,0x1234,0xabcd);
 unsigned char b[16];assert(recv(s,b,sizeof b,0)==10);
 unsigned char expected[]={ 'X','R',1,0,0,0,0x34,0x12,0xcd,0xab };
 assert(!memcmp(b,expected,10));
 gamepad_rumble(1,1,1);assert(recv(s,b,sizeof b,0)<0 && errno==EAGAIN);
 gamepad_rumble_stop();assert(recv(s,b,sizeof b,0)==10);
 assert(!b[6]&&!b[7]&&!b[8]&&!b[9]&&!gamepad_rumble_available);
 close(s);unlink(a.sun_path);
 puts("MOONLIGHT_MAPPING_AND_RUMBLE_PACKET_PASS");return 0;
}
