#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static unsigned get16(const unsigned char *p){return p[0]|((unsigned)p[1]<<8);}
static uint32_t get32(const unsigned char *p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
int main(int argc,char **argv){
 if(argc!=3)return 2;
 char *end;long gain=strtol(argv[2],&end,10);if(*end||gain<0||gain>100)return 2;
 FILE *f=fopen(argv[1],"rb");if(!f)return 1;
 unsigned char header[44];if(fread(header,1,44,f)!=44||memcmp(header,"RIFF",4)||memcmp(header+8,"WAVEfmt ",8)||get32(header+16)!=16||get16(header+20)!=1||get16(header+22)!=2||get32(header+24)!=48000||get16(header+34)!=16||memcmp(header+36,"data",4)){fclose(f);return 2;}
 uint32_t left=get32(header+40);if(!left||left>48000*4*8||left%4){fclose(f);return 2;}
 if(fwrite(header,1,44,stdout)!=44){fclose(f);return 1;}
 unsigned char b[4096];
 while(left){size_t n=left>sizeof b?sizeof b:left;if(fread(b,1,n,f)!=n){fclose(f);return 1;}
  for(size_t i=0;i<n;i+=2){int16_t sample=(int16_t)get16(b+i);sample=(int16_t)((int)sample*gain/100);b[i]=(unsigned char)sample;b[i+1]=(unsigned char)((uint16_t)sample>>8);}
  if(fwrite(b,1,n,stdout)!=n){fclose(f);return 1;}left-=(uint32_t)n;
 }
 fclose(f);return fflush(stdout)?1:0;
}
