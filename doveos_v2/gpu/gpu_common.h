#ifndef DOVEOS_GPU_COMMON_H
#define DOVEOS_GPU_COMMON_H
#define _GNU_SOURCE
#define EGL_EGLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

/* Private native pixmap ABI, confirmed against this r4p1 blob only. */
typedef struct { uint32_t h,w,bytespp; uint8_t bits,r,g,b,a,l,pad[2];
    uint32_t flags; void *memory; uint32_t yuv; } pixmap;
_Static_assert(sizeof(pixmap)==32,"native pixmap ABI");
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static GLuint compile(GLenum kind,const char *text){
    GLuint s=glCreateShader(kind);glShaderSource(s,1,&text,NULL);glCompileShader(s);
    GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[2048];glGetShaderInfoLog(s,sizeof(log),NULL,log);printf("SHADER_ERROR %s\n",log);glDeleteShader(s);return 0;}return s;
}
static GLuint program(const char *v,const char *f){
    GLuint vs=compile(GL_VERTEX_SHADER,v),fs=compile(GL_FRAGMENT_SHADER,f);
    if(!vs||!fs){if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);return 0;}
    GLuint p=glCreateProgram();glAttachShader(p,vs);glAttachShader(p,fs);
    glBindAttribLocation(p,0,"p");glLinkProgram(p);glDeleteShader(vs);glDeleteShader(fs);
    GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){char log[2048];glGetProgramInfoLog(p,sizeof(log),NULL,log);printf("LINK_ERROR %s\n",log);glDeleteProgram(p);return 0;}return p;
}
/* Texture lookup coordinates are computed in the vertex stage, then used
 * directly. No fragment coordinate arithmetic can round x=1024 and above. */
static const char *vertex=
 "attribute vec2 p;uniform vec2 size;uniform float chroma;"
 "varying highp vec2 t0,t1,t2,t3,t4,t5,t6,t7;"
 "void main(){vec2 uv=p*.5+.5;float dx=1./size.x,dy=chroma*.5/size.y;"
 "t0=uv+vec2(-1.5*dx,-dy);t1=uv+vec2(-.5*dx,-dy);"
 "t2=uv+vec2(.5*dx,-dy);t3=uv+vec2(1.5*dx,-dy);"
 "t4=uv+vec2(-1.5*dx,dy);t5=uv+vec2(-.5*dx,dy);"
 "t6=uv+vec2(.5*dx,dy);t7=uv+vec2(1.5*dx,dy);gl_Position=vec4(p,0.,1.);}"
 ;
static const char *frag_y=
 "precision mediump float;uniform sampler2D image;"
 "varying highp vec2 t0,t1,t2,t3,t4,t5,t6,t7;"
 "float y(vec3 c){return (16.+dot(c,vec3(65.738,129.057,25.064)))/255.;}"
 "void main(){gl_FragColor=vec4(y(texture2D(image,t2).rgb),"
 "y(texture2D(image,t1).rgb),y(texture2D(image,t0).rgb),y(texture2D(image,t3).rgb));}";
static const char *frag_uv=
 "precision mediump float;uniform sampler2D image;"
 "varying highp vec2 t0,t1,t2,t3,t4,t5,t6,t7;"
 "vec2 uv(vec3 c){return (vec2(128.)+vec2(dot(c,vec3(-37.945,-74.494,112.439)),"
 "dot(c,vec3(112.439,-94.154,-18.285))))/255.;}"
 "void main(){vec2 a=uv((texture2D(image,t0).rgb+texture2D(image,t1).rgb+"
 "texture2D(image,t4).rgb+texture2D(image,t5).rgb)*.25);"
 "vec2 b=uv((texture2D(image,t2).rgb+texture2D(image,t3).rgb+"
 "texture2D(image,t6).rgb+texture2D(image,t7).rgb)*.25);"
 "gl_FragColor=vec4(b.x,a.y,a.x,b.y);}";

static void fill(unsigned char *rgb,int w,int h,int pattern){
    for(int y=0;y<h;y++)for(int x=0;x<w;x++){
        unsigned char *p=rgb+((size_t)y*w+x)*4;
        if(pattern==1){p[0]=(x%2)*255;p[1]=(y%2)*255;p[2]=77;}
        else if(pattern==2){p[0]=(x*37+y*19)&255;p[1]=(x*17+y*11)&255;p[2]=(((x/13)^(y/7))*29)&255;}
        else{p[0]=77;p[1]=128;p[2]=205;}p[3]=255;
    }
}
static int rounded(float f){int n=(int)(f+.5f);return n<0?0:n>255?255:n;}
static unsigned validate(const unsigned char *rgb,const unsigned char *out,int w,int h){
    unsigned bad=0;int maximum=0;
    for(int y=0;y<h;y++)for(int x=0;x<w;x++){
        const unsigned char *p=rgb+((size_t)y*w+x)*4;
        int expected=rounded(16.f+(65.738f*p[0]+129.057f*p[1]+25.064f*p[2])/255.f);
        int delta=abs((int)out[(size_t)y*w+x]-expected);if(delta>maximum)maximum=delta;
        if(delta>1){if(!bad)printf("BAD_Y x=%d y=%d got=%u expected=%d\n",x,y,out[(size_t)y*w+x],expected);bad++;}
    }
    for(int y=0;y<h;y+=2)for(int x=0;x<w;x+=2){
        float c[3]={0};for(int dy=0;dy<2;dy++)for(int dx=0;dx<2;dx++)
            for(int k=0;k<3;k++)c[k]+=rgb[((size_t)(y+dy)*w+x+dx)*4+k]*.25f;
        int expected[2]={rounded(128.f+(-37.945f*c[0]-74.494f*c[1]+112.439f*c[2])/255.f),
          rounded(128.f+(112.439f*c[0]-94.154f*c[1]-18.285f*c[2])/255.f)};
        for(int k=0;k<2;k++){
            size_t j=(size_t)w*h+(y/2)*w+x+k;int delta=abs((int)out[j]-expected[k]);
            if(delta>maximum)maximum=delta;if(delta>1){if(!bad)printf("BAD_UV x=%d y=%d component=%d got=%u expected=%d\n",x,y,k,out[j],expected[k]);bad++;}
        }
    }
    printf("PIXELS bad=%u max_difference=%d total=%d\n",bad,maximum,w*h*3/2);return bad;
}

#endif
