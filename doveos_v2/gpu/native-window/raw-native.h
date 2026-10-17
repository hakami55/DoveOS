#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <dlfcn.h>
static __typeof__(&eglChooseConfig) raw_eglChooseConfig;
static __typeof__(&eglCreateContext) raw_eglCreateContext;
static __typeof__(&eglCreatePbufferSurface) raw_eglCreatePbufferSurface;
static __typeof__(&eglDestroyContext) raw_eglDestroyContext;
static __typeof__(&eglDestroySurface) raw_eglDestroySurface;
static __typeof__(&eglGetCurrentContext) raw_eglGetCurrentContext;
static __typeof__(&eglGetCurrentDisplay) raw_eglGetCurrentDisplay;
static __typeof__(&eglGetCurrentSurface) raw_eglGetCurrentSurface;
static __typeof__(&eglGetDisplay) raw_eglGetDisplay;
static __typeof__(&eglGetError) raw_eglGetError;
static __typeof__(&eglGetProcAddress) raw_eglGetProcAddress;
static __typeof__(&eglInitialize) raw_eglInitialize;
static __typeof__(&eglMakeCurrent) raw_eglMakeCurrent;
static __typeof__(&eglTerminate) raw_eglTerminate;
static __typeof__(&glAttachShader) raw_glAttachShader;
static __typeof__(&glBindAttribLocation) raw_glBindAttribLocation;
static __typeof__(&glBindFramebuffer) raw_glBindFramebuffer;
static __typeof__(&glBindTexture) raw_glBindTexture;
static __typeof__(&glCheckFramebufferStatus) raw_glCheckFramebufferStatus;
static __typeof__(&glClear) raw_glClear;
static __typeof__(&glClearColor) raw_glClearColor;
static __typeof__(&glCompileShader) raw_glCompileShader;
static __typeof__(&glCreateProgram) raw_glCreateProgram;
static __typeof__(&glCreateShader) raw_glCreateShader;
static __typeof__(&glDeleteFramebuffers) raw_glDeleteFramebuffers;
static __typeof__(&glDeleteProgram) raw_glDeleteProgram;
static __typeof__(&glDeleteShader) raw_glDeleteShader;
static __typeof__(&glDeleteTextures) raw_glDeleteTextures;
static __typeof__(&glDisable) raw_glDisable;
static __typeof__(&glDisableVertexAttribArray) raw_glDisableVertexAttribArray;
static __typeof__(&glDrawArrays) raw_glDrawArrays;
static __typeof__(&glEnable) raw_glEnable;
static __typeof__(&glEnableVertexAttribArray) raw_glEnableVertexAttribArray;
static __typeof__(&glFinish) raw_glFinish;
static __typeof__(&glFlush) raw_glFlush;
static __typeof__(&glFramebufferTexture2D) raw_glFramebufferTexture2D;
static __typeof__(&glGenFramebuffers) raw_glGenFramebuffers;
static __typeof__(&glGenTextures) raw_glGenTextures;
static __typeof__(&glGetError) raw_glGetError;
static __typeof__(&glGetIntegerv) raw_glGetIntegerv;
static __typeof__(&glGetProgramiv) raw_glGetProgramiv;
static __typeof__(&glGetShaderiv) raw_glGetShaderiv;
static __typeof__(&glGetTexParameteriv) raw_glGetTexParameteriv;
static __typeof__(&glGetUniformLocation) raw_glGetUniformLocation;
static __typeof__(&glIsEnabled) raw_glIsEnabled;
static __typeof__(&glLinkProgram) raw_glLinkProgram;
static __typeof__(&glReadPixels) raw_glReadPixels;
static __typeof__(&glScissor) raw_glScissor;
static __typeof__(&glShaderSource) raw_glShaderSource;
static __typeof__(&glUniform1f) raw_glUniform1f;
static __typeof__(&glUniform1i) raw_glUniform1i;
static __typeof__(&glUniform2f) raw_glUniform2f;
static __typeof__(&glUseProgram) raw_glUseProgram;
static __typeof__(&glVertexAttribPointer) raw_glVertexAttribPointer;
static __typeof__(&glViewport) raw_glViewport;
static __typeof__(&glUniform4f) raw_glUniform4f;
static __typeof__(&glActiveTexture) raw_glActiveTexture;
static __typeof__(&glTexParameteri) raw_glTexParameteri;
static int raw_load(void*lib){
 *(void**)&raw_glTexParameteri=dlsym(lib,"glTexParameteri");if(!raw_glTexParameteri)return 0;
 *(void**)&raw_glActiveTexture=dlsym(lib,"glActiveTexture");if(!raw_glActiveTexture)return 0;
 *(void**)&raw_glUniform4f=dlsym(lib,"glUniform4f");if(!raw_glUniform4f)return 0;
 *(void**)&raw_eglChooseConfig=dlsym(lib,"eglChooseConfig");if(!raw_eglChooseConfig)return 0;
 *(void**)&raw_eglCreateContext=dlsym(lib,"eglCreateContext");if(!raw_eglCreateContext)return 0;
 *(void**)&raw_eglCreatePbufferSurface=dlsym(lib,"eglCreatePbufferSurface");if(!raw_eglCreatePbufferSurface)return 0;
 *(void**)&raw_eglDestroyContext=dlsym(lib,"eglDestroyContext");if(!raw_eglDestroyContext)return 0;
 *(void**)&raw_eglDestroySurface=dlsym(lib,"eglDestroySurface");if(!raw_eglDestroySurface)return 0;
 *(void**)&raw_eglGetCurrentContext=dlsym(lib,"eglGetCurrentContext");if(!raw_eglGetCurrentContext)return 0;
 *(void**)&raw_eglGetCurrentDisplay=dlsym(lib,"eglGetCurrentDisplay");if(!raw_eglGetCurrentDisplay)return 0;
 *(void**)&raw_eglGetCurrentSurface=dlsym(lib,"eglGetCurrentSurface");if(!raw_eglGetCurrentSurface)return 0;
 *(void**)&raw_eglGetDisplay=dlsym(lib,"eglGetDisplay");if(!raw_eglGetDisplay)return 0;
 *(void**)&raw_eglGetError=dlsym(lib,"eglGetError");if(!raw_eglGetError)return 0;
 *(void**)&raw_eglGetProcAddress=dlsym(lib,"eglGetProcAddress");if(!raw_eglGetProcAddress)return 0;
 *(void**)&raw_eglInitialize=dlsym(lib,"eglInitialize");if(!raw_eglInitialize)return 0;
 *(void**)&raw_eglMakeCurrent=dlsym(lib,"eglMakeCurrent");if(!raw_eglMakeCurrent)return 0;
 *(void**)&raw_eglTerminate=dlsym(lib,"eglTerminate");if(!raw_eglTerminate)return 0;
 *(void**)&raw_glAttachShader=dlsym(lib,"glAttachShader");if(!raw_glAttachShader)return 0;
 *(void**)&raw_glBindAttribLocation=dlsym(lib,"glBindAttribLocation");if(!raw_glBindAttribLocation)return 0;
 *(void**)&raw_glBindFramebuffer=dlsym(lib,"glBindFramebuffer");if(!raw_glBindFramebuffer)return 0;
 *(void**)&raw_glBindTexture=dlsym(lib,"glBindTexture");if(!raw_glBindTexture)return 0;
 *(void**)&raw_glCheckFramebufferStatus=dlsym(lib,"glCheckFramebufferStatus");if(!raw_glCheckFramebufferStatus)return 0;
 *(void**)&raw_glClear=dlsym(lib,"glClear");if(!raw_glClear)return 0;
 *(void**)&raw_glClearColor=dlsym(lib,"glClearColor");if(!raw_glClearColor)return 0;
 *(void**)&raw_glCompileShader=dlsym(lib,"glCompileShader");if(!raw_glCompileShader)return 0;
 *(void**)&raw_glCreateProgram=dlsym(lib,"glCreateProgram");if(!raw_glCreateProgram)return 0;
 *(void**)&raw_glCreateShader=dlsym(lib,"glCreateShader");if(!raw_glCreateShader)return 0;
 *(void**)&raw_glDeleteFramebuffers=dlsym(lib,"glDeleteFramebuffers");if(!raw_glDeleteFramebuffers)return 0;
 *(void**)&raw_glDeleteProgram=dlsym(lib,"glDeleteProgram");if(!raw_glDeleteProgram)return 0;
 *(void**)&raw_glDeleteShader=dlsym(lib,"glDeleteShader");if(!raw_glDeleteShader)return 0;
 *(void**)&raw_glDeleteTextures=dlsym(lib,"glDeleteTextures");if(!raw_glDeleteTextures)return 0;
 *(void**)&raw_glDisable=dlsym(lib,"glDisable");if(!raw_glDisable)return 0;
 *(void**)&raw_glDisableVertexAttribArray=dlsym(lib,"glDisableVertexAttribArray");if(!raw_glDisableVertexAttribArray)return 0;
 *(void**)&raw_glDrawArrays=dlsym(lib,"glDrawArrays");if(!raw_glDrawArrays)return 0;
 *(void**)&raw_glEnable=dlsym(lib,"glEnable");if(!raw_glEnable)return 0;
 *(void**)&raw_glEnableVertexAttribArray=dlsym(lib,"glEnableVertexAttribArray");if(!raw_glEnableVertexAttribArray)return 0;
 *(void**)&raw_glFinish=dlsym(lib,"glFinish");if(!raw_glFinish)return 0;
 *(void**)&raw_glFlush=dlsym(lib,"glFlush");if(!raw_glFlush)return 0;
 *(void**)&raw_glFramebufferTexture2D=dlsym(lib,"glFramebufferTexture2D");if(!raw_glFramebufferTexture2D)return 0;
 *(void**)&raw_glGenFramebuffers=dlsym(lib,"glGenFramebuffers");if(!raw_glGenFramebuffers)return 0;
 *(void**)&raw_glGenTextures=dlsym(lib,"glGenTextures");if(!raw_glGenTextures)return 0;
 *(void**)&raw_glGetError=dlsym(lib,"glGetError");if(!raw_glGetError)return 0;
 *(void**)&raw_glGetIntegerv=dlsym(lib,"glGetIntegerv");if(!raw_glGetIntegerv)return 0;
 *(void**)&raw_glGetProgramiv=dlsym(lib,"glGetProgramiv");if(!raw_glGetProgramiv)return 0;
 *(void**)&raw_glGetShaderiv=dlsym(lib,"glGetShaderiv");if(!raw_glGetShaderiv)return 0;
 *(void**)&raw_glGetTexParameteriv=dlsym(lib,"glGetTexParameteriv");if(!raw_glGetTexParameteriv)return 0;
 *(void**)&raw_glGetUniformLocation=dlsym(lib,"glGetUniformLocation");if(!raw_glGetUniformLocation)return 0;
 *(void**)&raw_glIsEnabled=dlsym(lib,"glIsEnabled");if(!raw_glIsEnabled)return 0;
 *(void**)&raw_glLinkProgram=dlsym(lib,"glLinkProgram");if(!raw_glLinkProgram)return 0;
 *(void**)&raw_glReadPixels=dlsym(lib,"glReadPixels");if(!raw_glReadPixels)return 0;
 *(void**)&raw_glScissor=dlsym(lib,"glScissor");if(!raw_glScissor)return 0;
 *(void**)&raw_glShaderSource=dlsym(lib,"glShaderSource");if(!raw_glShaderSource)return 0;
 *(void**)&raw_glUniform1f=dlsym(lib,"glUniform1f");if(!raw_glUniform1f)return 0;
 *(void**)&raw_glUniform1i=dlsym(lib,"glUniform1i");if(!raw_glUniform1i)return 0;
 *(void**)&raw_glUniform2f=dlsym(lib,"glUniform2f");if(!raw_glUniform2f)return 0;
 *(void**)&raw_glUseProgram=dlsym(lib,"glUseProgram");if(!raw_glUseProgram)return 0;
 *(void**)&raw_glVertexAttribPointer=dlsym(lib,"glVertexAttribPointer");if(!raw_glVertexAttribPointer)return 0;
 *(void**)&raw_glViewport=dlsym(lib,"glViewport");if(!raw_glViewport)return 0;
 return 1;
}
#define eglChooseConfig raw_eglChooseConfig
#define eglCreateContext raw_eglCreateContext
#define eglCreatePbufferSurface raw_eglCreatePbufferSurface
#define eglDestroyContext raw_eglDestroyContext
#define eglDestroySurface raw_eglDestroySurface
#define eglGetCurrentContext raw_eglGetCurrentContext
#define eglGetCurrentDisplay raw_eglGetCurrentDisplay
#define eglGetCurrentSurface raw_eglGetCurrentSurface
#define eglGetDisplay raw_eglGetDisplay
#define eglGetError raw_eglGetError
#define eglGetProcAddress raw_eglGetProcAddress
#define eglInitialize raw_eglInitialize
#define eglMakeCurrent raw_eglMakeCurrent
#define eglTerminate raw_eglTerminate
#define glAttachShader raw_glAttachShader
#define glBindAttribLocation raw_glBindAttribLocation
#define glBindFramebuffer raw_glBindFramebuffer
#define glBindTexture raw_glBindTexture
#define glCheckFramebufferStatus raw_glCheckFramebufferStatus
#define glClear raw_glClear
#define glClearColor raw_glClearColor
#define glCompileShader raw_glCompileShader
#define glCreateProgram raw_glCreateProgram
#define glCreateShader raw_glCreateShader
#define glDeleteFramebuffers raw_glDeleteFramebuffers
#define glDeleteProgram raw_glDeleteProgram
#define glDeleteShader raw_glDeleteShader
#define glDeleteTextures raw_glDeleteTextures
#define glDisable raw_glDisable
#define glDisableVertexAttribArray raw_glDisableVertexAttribArray
#define glDrawArrays raw_glDrawArrays
#define glEnable raw_glEnable
#define glEnableVertexAttribArray raw_glEnableVertexAttribArray
#define glFinish raw_glFinish
#define glFlush raw_glFlush
#define glFramebufferTexture2D raw_glFramebufferTexture2D
#define glGenFramebuffers raw_glGenFramebuffers
#define glGenTextures raw_glGenTextures
#define glGetError raw_glGetError
#define glGetIntegerv raw_glGetIntegerv
#define glGetProgramiv raw_glGetProgramiv
#define glGetShaderiv raw_glGetShaderiv
#define glGetTexParameteriv raw_glGetTexParameteriv
#define glGetUniformLocation raw_glGetUniformLocation
#define glIsEnabled raw_glIsEnabled
#define glLinkProgram raw_glLinkProgram
#define glReadPixels raw_glReadPixels
#define glScissor raw_glScissor
#define glShaderSource raw_glShaderSource
#define glUniform1f raw_glUniform1f
#define glUniform1i raw_glUniform1i
#define glUniform2f raw_glUniform2f
#define glUseProgram raw_glUseProgram
#define glVertexAttribPointer raw_glVertexAttribPointer
#define glViewport raw_glViewport

#define glUniform4f raw_glUniform4f

#define glActiveTexture raw_glActiveTexture

#define glTexParameteri raw_glTexParameteri
