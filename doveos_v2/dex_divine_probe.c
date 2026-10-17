#define _GNU_SOURCE
#include <dlfcn.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
/* Use the vendor helper, whose ARM disassembly confirms its own event ABI.
   This tool never constructs or writes a guessed DFBInputEvent. */
int main(int argc,char **argv){
 alarm(5);
 if(argc!=2||(strcmp(argv[1],"--check")&&strcmp(argv[1],"--pointer-once")))return 2;
 void *lib=dlopen("/usr/lib/libdivine-1.6.so.0.0.3",RTLD_NOW|RTLD_LOCAL);
 if(!lib){fprintf(stderr,"DIVINE_LOAD_FAILED %s\n",dlerror());return 1;}
 void *(*open_pipe)(const char *)=dlsym(lib,"divine_open");
 void (*motion)(void *,int,int)=dlsym(lib,"divine_send_motion_absolute");
 void (*close_pipe)(void *)=dlsym(lib,"divine_close");
 if(!open_pipe||!motion||!close_pipe)return 3;
 if(!strcmp(argv[1],"--check")){puts("DIVINE_VENDOR_HELPERS_READY");dlclose(lib);return 0;}
 void *pipe=open_pipe("/tmp/divine");if(!pipe)return 4;
 motion(pipe,640,360);close_pipe(pipe);dlclose(lib);
 puts("DIVINE_POINTER_EVENT_SENT no_keys_or_clicks");return 0;
}
