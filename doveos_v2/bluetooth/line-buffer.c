#include <stdio.h>
__attribute__((constructor)) static void init(void){setvbuf(stdout,NULL,_IOLBF,0);}
