#define main bridge_main
#include "xbox_usb.c"
#undef main
#include <assert.h>
int main(void) {
 int pipes[2];assert(pipe(pipes)==0);unsigned seen=0;
 unsigned char b[18]={0x20,0,0,14,0xfc,0xff,0xff,3,0xff,3,0,0x80,0xff,0x7f,0xff,0x7f,0,0x80};
 assert(decode_input(pipes[1],b,17,&seen)==0);
 assert(decode_input(pipes[1],b,18,&seen)==1 && seen==0x3ff);
 struct input_event e[19];assert(read(pipes[0],e,sizeof e)==sizeof e);
 assert(e[2].code==BTN_X && e[2].value==1 && e[3].code==BTN_Y);
 assert(e[10].code==ABS_X && e[10].value==-32768);
 assert(e[11].code==ABS_Y && e[11].value==-32768);
 assert(e[12].code==ABS_RX && e[12].value==32767);
 assert(e[13].code==ABS_RY && e[13].value==32767);
 assert(e[14].value==1023 && e[15].value==1023);
 assert(e[18].type==EV_SYN);
 b[7]=4;assert(decode_input(pipes[1],b,18,&seen)==0);
 Rumble r={0};double before=now();rumble_start(&r,123,456,9999,9999);
 assert(r.begins>=before+4.99 && r.begins<=before+5.1);
 assert(r.ends-r.begins==5 && r.strong==123 && r.weak==456);
 puts("PROTOCOL_TEST_PASS length buttons signed axes triggers rumble bounds");return 0;
}
