#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
int main(void) {
 int fd=open("/dev/dri/card0",O_RDWR|O_CLOEXEC);
 struct gbm_device *gbm=gbm_create_device(fd);
 PFNEGLGETPLATFORMDISPLAYEXTPROC get=(void*)eglGetProcAddress("eglGetPlatformDisplayEXT");
 EGLDisplay d=get(EGL_PLATFORM_GBM_KHR,gbm,NULL); int major,minor,n;
 if (!eglInitialize(d,&major,&minor)) {printf("eglInitialize: %x\n",eglGetError()); return 1;}
 printf("EGL %d.%d %s\n%s\n",major,minor,eglQueryString(d,EGL_VENDOR),eglQueryString(d,EGL_EXTENSIONS));
 eglBindAPI(EGL_OPENGL_ES_API);
 EGLint attrs[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_NONE}; EGLConfig cfg;
 if(!eglChooseConfig(d,attrs,&cfg,1,&n)||!n) return 2;
 EGLint ca[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
 EGLContext c=eglCreateContext(d,cfg,EGL_NO_CONTEXT,ca);
 if(c==EGL_NO_CONTEXT || !eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,c)) {printf("context: %x\n",eglGetError());return 3;}
 printf("GL %s\nGPU %s\n",glGetString(GL_VERSION),glGetString(GL_RENDERER));
 GLint v,f; glGetIntegerv(GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS,&v); glGetIntegerv(GL_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS,&f); printf("SSBO vertex=%d fragment=%d\n",v,f);
 eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT); eglDestroyContext(d,c); eglTerminate(d); gbm_device_destroy(gbm); close(fd); return 0;
}
