#include <sys/swap.h>
#include <stdio.h>
int main(void){
 if(swapon("/media/usb0/DoveOS-v2/Memory/browser-swap.bin",SWAP_FLAG_PREFER|100)){perror("browser swap");return 1;}
 return 0;
}
