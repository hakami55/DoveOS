#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
int main(int argc,char **argv){if(argc!=2)return 2;struct timespec t={(time_t)strtoll(argv[1],NULL,10),0};if(t.tv_sec<1577836800)return 2;if(clock_settime(CLOCK_REALTIME,&t)){perror("set clock");return 1;}printf("CLOCK_SET=%ld\n",(long)t.tv_sec);return 0;}
