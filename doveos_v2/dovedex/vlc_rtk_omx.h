/* OMX lifecycle reused from the tested Moonlight backend. */


#include <OMX_Component.h>
#include <OMX_Video.h>
#include <arm_neon.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <semaphore.h>
#include <errno.h>
static sem_t input_done;

/* The Realtek component is used only by the isolated receiver child.  The
   vendor's OMX_Deinit crashes after release; process exit owns final cleanup. */
static OMX_COMPONENTTYPE *component;
static OMX_BUFFERHEADERTYPE *in[8],*out[8];
static OMX_PARAM_PORTDEFINITIONTYPE ports[2];
static volatile int returned,port_change,disabled,enabled,hw_failed;
static OMX_BUFFERHEADERTYPE *ready_queue[16];
static uint64_t ready_times[16],last_ready_time;
static unsigned ready_read,ready_write;
static OMX_BUFFERHEADERTYPE *pop_output(void){
    unsigned r=__atomic_load_n(&ready_read,__ATOMIC_RELAXED);
    if(r==__atomic_load_n(&ready_write,__ATOMIC_ACQUIRE))return NULL;
    OMX_BUFFERHEADERTYPE *b=ready_queue[r%16];last_ready_time=ready_times[r%16];
    __atomic_store_n(&ready_read,r+1,__ATOMIC_RELEASE);return b;
}
static int hw_hevc;
static int configured;
static pthread_t output_thread;
static int output_stop;
static void *output_loop(void *unused);
static int output_stride=1280,output_slice=720;





static double seconds(void){
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return t.tv_sec+t.tv_nsec/1000000000.0;
}

static OMX_VERSIONTYPE omx_version(void){
    OMX_VERSIONTYPE v;memset(&v,0,sizeof v);
    v.s.nVersionMajor=1;v.s.nVersionMinor=1;v.s.nRevision=2;return v;
}
static void delay_ms(long ms){
    struct timespec t={ms/1000,(ms%1000)*1000000};nanosleep(&t,NULL);
}
static int wait_value(volatile int *value,int target,int timeout_ms){
    for(int i=0;i<timeout_ms;i++){
        if(__atomic_load_n(value,__ATOMIC_ACQUIRE)>=target)return 0;
        if(__atomic_load_n(&hw_failed,__ATOMIC_ACQUIRE))return -1;
        delay_ms(1);
    }
    return -1;
}
static OMX_ERRORTYPE event_cb(OMX_HANDLETYPE h,OMX_PTR app,OMX_EVENTTYPE event,
                              OMX_U32 a,OMX_U32 b,OMX_PTR data){
    (void)h;(void)app;(void)data;
    if(event==OMX_EventError){
        printf("OMX_ERROR=0x%08x DETAIL=%u\n",(unsigned)a,(unsigned)b);
        __atomic_store_n(&hw_failed,1,__ATOMIC_RELEASE);
    }
    if(event==OMX_EventPortSettingsChanged && a==1)
        __atomic_store_n(&port_change,1,__ATOMIC_RELEASE);
    if(event==OMX_EventCmdComplete && a==OMX_CommandPortDisable && b==1)
        __atomic_store_n(&disabled,1,__ATOMIC_RELEASE);
    if(event==OMX_EventCmdComplete && a==OMX_CommandPortEnable && b==1)
        __atomic_store_n(&enabled,1,__ATOMIC_RELEASE);
    return OMX_ErrorNone;
}
static OMX_ERRORTYPE empty_cb(OMX_HANDLETYPE h,OMX_PTR app,OMX_BUFFERHEADERTYPE *b){
    (void)h;(void)app;(void)b;
    __atomic_add_fetch(&returned,1,__ATOMIC_RELEASE);sem_post(&input_done);return OMX_ErrorNone;
}
static OMX_ERRORTYPE fill_cb(OMX_HANDLETYPE h,OMX_PTR app,OMX_BUFFERHEADERTYPE *b){
    (void)h;(void)app;
    if(b->nFilledLen){
        unsigned w=__atomic_load_n(&ready_write,__ATOMIC_RELAXED);
        if(w-__atomic_load_n(&ready_read,__ATOMIC_ACQUIRE)>=16){hw_failed=1;ended=1;}
        else {ready_queue[w%16]=b;ready_times[w%16]=LiGetMicroseconds();__atomic_store_n(&ready_write,w+1,__ATOMIC_RELEASE);}
    }
    return OMX_ErrorNone;
}
static OMX_CALLBACKTYPE callbacks={event_cb,empty_cb,fill_cb};
static int wait_state(OMX_STATETYPE target){
    for(int i=0;i<5000;i++){
        OMX_STATETYPE state=OMX_StateInvalid;
        if(component->GetState(component,&state)!=OMX_ErrorNone)return -1;
        if(state==target)return 0;
        if(__atomic_load_n(&hw_failed,__ATOMIC_ACQUIRE))return -1;
        delay_ms(1);
    }
    return -1;
}
int omx_backend_init(void){
    if(sem_init(&input_done,0,0))return -1;
    void *core=dlopen("/usr/lib/libOMX_Core.so",RTLD_LAZY|RTLD_LOCAL|RTLD_DEEPBIND);
    void *osal=dlopen("/usr/lib/libRTKOMX_OSAL.so",RTLD_LAZY|RTLD_GLOBAL|RTLD_DEEPBIND);
    void *vpu=dlopen("/usr/lib/libvpu.so",RTLD_NOW|RTLD_GLOBAL|RTLD_DEEPBIND);
    void *decoder=dlopen("/usr/lib/libOMX.realtek.video.dec.so",RTLD_LAZY|RTLD_GLOBAL|RTLD_DEEPBIND);
    if(!core||!osal||!vpu||!decoder){
        printf("OMX_LIBRARY_LOAD_FAIL=%s CORE=%d OSAL=%d VPU=%d DECODER=%d\n",
               dlerror(),core!=NULL,osal!=NULL,vpu!=NULL,decoder!=NULL);
        return -1;
    }
    OMX_ERRORTYPE (*init)(void)=dlsym(core,"OMX_Init");
    OMX_ERRORTYPE (*constructor)(OMX_COMPONENTTYPE*,char*,int)=
        dlsym(decoder,"RTKHwOmx_constructor");
    if(!init||!constructor||init()!=OMX_ErrorNone)return -1;
    component=calloc(1,sizeof *component);if(!component)return -1;
    component->nSize=sizeof *component;
    char name[]="OMX.realtek.video.decoder";
    if(constructor(component,name,0)!=OMX_ErrorNone)return -1;
    if(component->SetCallbacks(component,&callbacks,NULL)!=OMX_ErrorNone)return -1;
    if(hw_hevc){
        OMX_PARAM_COMPONENTROLETYPE role;
        memset(&role,0,sizeof role);role.nSize=sizeof role;role.nVersion=omx_version();
        memcpy(role.cRole,"video_decoder.hevc",sizeof "video_decoder.hevc");
        OMX_ERRORTYPE e=component->SetParameter(component,OMX_IndexParamStandardComponentRole,&role);
        if(e!=OMX_ErrorNone){printf("HEVC_OMX_ROLE_REJECTED=0x%08x\n",(unsigned)e);component=NULL;return -1;}
        puts("HEVC_OMX_ROLE_ACCEPTED=1");
    }
    for(int port=0;port<2;port++){
        memset(&ports[port],0,sizeof ports[port]);
        ports[port].nSize=sizeof ports[port];
        ports[port].nVersion=omx_version();
        ports[port].nPortIndex=(OMX_U32)port;
        if(component->GetParameter(component,OMX_IndexParamPortDefinition,&ports[port])!=OMX_ErrorNone ||
           ports[port].nBufferCountActual>8 || ports[port].nBufferSize>5000000)return -1;
    }
    if(component->SendCommand(component,OMX_CommandStateSet,OMX_StateIdle,NULL)!=OMX_ErrorNone)return -1;
    for(int port=0;port<2;port++)for(OMX_U32 i=0;i<ports[port].nBufferCountActual;i++){
        OMX_BUFFERHEADERTYPE **slot=port?&out[i]:&in[i];
        if(component->AllocateBuffer(component,slot,(OMX_U32)port,NULL,
                                     ports[port].nBufferSize)!=OMX_ErrorNone)return -1;
    }
    if(wait_state(OMX_StateIdle))return -1;
    if(component->SendCommand(component,OMX_CommandStateSet,OMX_StateExecuting,NULL)!=OMX_ErrorNone ||
       wait_state(OMX_StateExecuting))return -1;
    for(OMX_U32 i=0;i<ports[1].nBufferCountActual;i++)
        if(component->FillThisBuffer(component,out[i])!=OMX_ErrorNone)return -1;
    puts("OMX_HARDWARE_READY=1");return 0;
}
static int resize_output(void){
    if(component->SendCommand(component,OMX_CommandPortDisable,1,NULL)!=OMX_ErrorNone)return -1;
    delay_ms(100);
    for(OMX_U32 i=0;i<ports[1].nBufferCountActual;i++){
        if(component->FreeBuffer(component,1,out[i])!=OMX_ErrorNone)return -1;
        out[i]=NULL;
    }
    if(wait_value(&disabled,1,2000))return -1;
    if(component->GetParameter(component,OMX_IndexParamPortDefinition,&ports[1])!=OMX_ErrorNone)return -1;
    output_stride=ports[1].format.video.nStride;
    output_slice=ports[1].format.video.nSliceHeight;
    if(!output_stride)output_stride=hw_width;
    if(!output_slice)output_slice=(int)(ports[1].nBufferSize/(output_stride*3/2));
    printf("OMX_OUTPUT_LAYOUT WIDTH=%u HEIGHT=%u STRIDE=%d SLICE=%d SIZE=%u COLOR=%d\n",
           (unsigned)ports[1].format.video.nFrameWidth,
           (unsigned)ports[1].format.video.nFrameHeight,output_stride,output_slice,
           (unsigned)ports[1].nBufferSize,(int)ports[1].format.video.eColorFormat);
    if(ports[1].format.video.nFrameWidth!=(OMX_U32)hw_width ||
       ports[1].format.video.nFrameHeight!=(OMX_U32)hw_height ||
       output_stride!=hw_width || output_slice<hw_height || output_slice>hw_height+32 ||
       ports[1].nBufferSize!=(OMX_U32)(output_stride*output_slice*3/2) ||
       ports[1].nBufferCountActual>8)return -1;
    if(component->SendCommand(component,OMX_CommandPortEnable,1,NULL)!=OMX_ErrorNone)return -1;
    for(OMX_U32 i=0;i<ports[1].nBufferCountActual;i++)
        if(component->AllocateBuffer(component,&out[i],1,NULL,
                                     ports[1].nBufferSize)!=OMX_ErrorNone)return -1;
    if(wait_value(&enabled,1,2000))return -1;
    for(OMX_U32 i=0;i<ports[1].nBufferCountActual;i++)
        if(component->FillThisBuffer(component,out[i])!=OMX_ErrorNone)return -1;
    __atomic_store_n(&configured,1,__ATOMIC_RELEASE);puts("OMX_NV12_CONFIGURED=1");return 0;
}

