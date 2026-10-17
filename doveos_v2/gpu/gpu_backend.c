/* Experimental display conversion. Existing GTK/X11 drawing remains CPU. */
#include "gpu_common.h"
#include "gpu_backend.h"
typedef struct {
    int w,h,ion,dma,initialized;size_t bytes,scratch_size;
    uint32_t handle,physical;uint8_t *cpu,*scratch;
    EGLDisplay display;EGLSurface surface;EGLContext context;EGLImageKHR image;
    void *base,*memory,*ion_library;
    GLuint fb,rb,tx,y_program,uv_program;
    pixmap native; /* EGL retains this descriptor until image destruction. */
    void (*base_destroy)(void*),(*unmap)(void*);
    int (*ion_free)(int,uint32_t);
} State;
void dove_gpu_destroy(void *opaque){
    State *s=opaque;if(!s)return;
    if(s->initialized){
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        if(s->y_program)glDeleteProgram(s->y_program);
        if(s->uv_program)glDeleteProgram(s->uv_program);
        if(s->tx)glDeleteTextures(1,&s->tx);
        if(s->fb)glDeleteFramebuffers(1,&s->fb);
        if(s->rb)glDeleteRenderbuffers(1,&s->rb);
    }
    if(s->image!=EGL_NO_IMAGE_KHR){PFNEGLDESTROYIMAGEKHRPROC f=(void*)eglGetProcAddress("eglDestroyImageKHR");if(f)f(s->display,s->image);}
    if(s->memory&&s->unmap)s->unmap(s->memory);
    if(s->base&&s->base_destroy)s->base_destroy(s->base);
    if(s->cpu!=MAP_FAILED)munmap(s->cpu,s->bytes);
    if(s->dma>=0)close(s->dma);
    if(s->handle&&s->ion_free)s->ion_free(s->ion,s->handle);
    if(s->ion>=0)close(s->ion);
    if(s->display!=EGL_NO_DISPLAY){
        eglMakeCurrent(s->display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        if(s->context!=EGL_NO_CONTEXT)eglDestroyContext(s->display,s->context);
        if(s->surface!=EGL_NO_SURFACE)eglDestroySurface(s->display,s->surface);
        eglTerminate(s->display);
    }
    if(s->ion_library)dlclose(s->ion_library);
    free(s->scratch);free(s);
}
static GLuint bgra_program(const char *source){
    char *copy=strdup(source);if(!copy)return 0;
    for(char *p=copy;(p=strstr(p,".rgb"));p+=4)memcpy(p,".bgr",4);
    GLuint result=program(vertex,copy);free(copy);return result;
}
void *dove_gpu_create(int w,int h){
    if(w<32||w>1920||w%4||h<32||h>1080||h%2)return NULL;
    State *s=calloc(1,sizeof(*s));if(!s)return NULL;
    s->w=w;s->h=h;s->ion=s->dma=-1;s->cpu=MAP_FAILED;
    s->display=EGL_NO_DISPLAY;s->surface=EGL_NO_SURFACE;s->context=EGL_NO_CONTEXT;s->image=EGL_NO_IMAGE_KHR;
    s->bytes=((size_t)w*h*3/2+4095)&~4095;
    void *(*base_create)(void)=dlsym(RTLD_DEFAULT,"_mali_base_common_context_create");
    void *(*map)(void*,uint32_t,uint32_t,void*,uint32_t)=dlsym(RTLD_DEFAULT,"_mali_base_common_mem_add_phys_mem");
    void (*share)(int,uint32_t,uint32_t)=dlsym(RTLD_DEFAULT,"gpu_share_buffer");
    s->base_destroy=dlsym(RTLD_DEFAULT,"_mali_base_common_context_destroy");s->unmap=dlsym(RTLD_DEFAULT,"_mali_base_common_mem_free");
    s->ion_library=dlopen("/usr/lib/libVOutWrapper.so",RTLD_LAZY|RTLD_LOCAL|RTLD_DEEPBIND);
    if(!s->ion_library)goto fail;
    int (*alloc)(int,size_t,size_t,uint32_t,uint32_t,uint32_t*)=dlsym(s->ion_library,"ion_alloc");
    int (*export_ion)(int,uint32_t,int*)=dlsym(s->ion_library,"ion_share");
    int (*physical)(int,uint32_t,uint32_t*,uint32_t*)=dlsym(s->ion_library,"ion_phys");
    s->ion_free=dlsym(s->ion_library,"ion_free");
    if(!base_create||!map||!share||!s->unmap||!s->base_destroy||!alloc||!export_ion||!physical||!s->ion_free)goto fail;
    s->display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if(!eglInitialize(s->display,NULL,NULL)||!eglBindAPI(EGL_OPENGL_ES_API))goto fail;
    EGLint a[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};EGLConfig cfg;EGLint count=0;
    if(!eglChooseConfig(s->display,a,&cfg,1,&count)||!count)goto fail;
    EGLint ca[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE},sa[]={EGL_WIDTH,32,EGL_HEIGHT,32,EGL_NONE};
    s->context=eglCreateContext(s->display,cfg,EGL_NO_CONTEXT,ca);s->surface=eglCreatePbufferSurface(s->display,cfg,sa);
    if(!eglMakeCurrent(s->display,s->surface,s->surface,s->context))goto fail;s->initialized=1;
    uint32_t actual=0;s->ion=open("/dev/ion",O_RDWR|O_CLOEXEC);
    if(s->ion<0||alloc(s->ion,s->bytes,4096,0x80,0x80,&s->handle)||physical(s->ion,s->handle,&s->physical,&actual)||actual!=s->bytes||export_ion(s->ion,s->handle,&s->dma))goto fail;
    s->cpu=mmap(NULL,s->bytes,PROT_READ|PROT_WRITE,MAP_SHARED,s->dma,0);if(s->cpu==MAP_FAILED)goto fail;
    memset(s->cpu,0,s->bytes);share(s->dma,s->physical,actual);s->base=base_create();if(!s->base)goto fail;
    s->memory=map(s->base,s->physical,actual,s->cpu,0x32);if(!s->memory)goto fail;
    s->native=(pixmap){.h=h*3/2,.w=w/4,.bytespp=4,.bits=32,.r=8,.g=8,.b=8,.a=8,.flags=1,.memory=s->memory};
    PFNEGLCREATEIMAGEKHRPROC create=(void*)eglGetProcAddress("eglCreateImageKHR");PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC attach=(void*)eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES");
    EGLint ia[]={EGL_IMAGE_PRESERVED_KHR,EGL_TRUE,EGL_NONE};if(!create||!attach)goto fail;
    s->image=create(s->display,EGL_NO_CONTEXT,EGL_NATIVE_PIXMAP_KHR,&s->native,ia);if(!s->image)goto fail;
    glGenRenderbuffers(1,&s->rb);glBindRenderbuffer(GL_RENDERBUFFER,s->rb);attach(GL_RENDERBUFFER,s->image);
    glGenFramebuffers(1,&s->fb);glBindFramebuffer(GL_FRAMEBUFFER,s->fb);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,s->rb);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)goto fail;
    s->y_program=bgra_program(frag_y);s->uv_program=bgra_program(frag_uv);if(!s->y_program||!s->uv_program)goto fail;
    glGenTextures(1,&s->tx);glBindTexture(GL_TEXTURE_2D,s->tx);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    for(int i=0;i<2;i++){GLuint p=i?s->uv_program:s->y_program;glUseProgram(p);glUniform2f(glGetUniformLocation(p,"size"),w,h);glUniform1f(glGetUniformLocation(p,"chroma"),i);}
    if(glGetError())goto fail;
    printf("GPU_BACKEND_READY renderer=%s size=%dx%d\n",glGetString(GL_RENDERER),w,h);return s;
fail:
    printf("GPU_BACKEND_INIT_FAILED EGL=%x errno=%d\n",eglGetError(),errno);dove_gpu_destroy(s);return NULL;
}
int dove_gpu_convert(void *opaque,const uint8_t *bgra,uint8_t *nv12,int left,int top,int right,int bottom){
    State *s=opaque;if(!s||!bgra||!nv12||left<0||top<0||right>s->w||bottom>s->h||left>=right||top>=bottom)return -1;
    left&=~3;top&=~1;right=(right+3)&~3;bottom=(bottom+1)&~1;
    int width=right-left,height=bottom-top;const uint8_t *input=bgra+((size_t)top*s->w+left)*4;
    if(width!=s->w){
        size_t needed=(size_t)width*height*4;
        if(needed>s->scratch_size){uint8_t *next=realloc(s->scratch,needed);if(!next)return -1;s->scratch=next;s->scratch_size=needed;}
        for(int row=0;row<height;row++)memcpy(s->scratch+(size_t)row*width*4,input+(size_t)row*s->w*4,width*4);input=s->scratch;
    }
    glBindTexture(GL_TEXTURE_2D,s->tx);glTexSubImage2D(GL_TEXTURE_2D,0,left,top,width,height,GL_RGBA,GL_UNSIGNED_BYTE,input);
    GLfloat q[]={-1,-1,1,-1,-1,1,1,1};glVertexAttribPointer(0,2,GL_FLOAT,0,0,q);glEnableVertexAttribArray(0);glEnable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER,s->fb);glUseProgram(s->y_program);glViewport(0,0,s->w/4,s->h);glScissor(left/4,top,width/4,height);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    glUseProgram(s->uv_program);glViewport(0,s->h,s->w/4,s->h/2);glScissor(left/4,s->h+top/2,width/4,height/2);glDrawArrays(GL_TRIANGLE_STRIP,0,4);glFinish();
    glDisable(GL_SCISSOR_TEST);glDisableVertexAttribArray(0);
    if(glGetError()){puts("GPU_BACKEND_DRAW_FAILED");return -1;}
    /* Copy only dirty plane bytes into the bridge's cached NV12 image. */
    for(int row=top;row<bottom;row++)memcpy(nv12+(size_t)row*s->w+left,s->cpu+(size_t)row*s->w+left,width);
    for(int row=top/2;row<bottom/2;row++)memcpy(nv12+(size_t)s->w*s->h+(size_t)row*s->w+left,s->cpu+(size_t)s->w*s->h+(size_t)row*s->w+left,width);
    return 0;
}
