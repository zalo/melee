// Direct GLES control: compare VBO and integer-texture vertex pulling without Dawn.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
static GLuint shader(GLenum kind,const char* text) {
 GLuint s=glCreateShader(kind);glShaderSource(s,1,&text,NULL);glCompileShader(s);
 GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
 if(!ok){char log[2048];glGetShaderInfoLog(s,sizeof(log),NULL,log);fprintf(stderr,"%s\n",log);exit(2);}return s;
}
int main(void) {
 int surfaceless=getenv("MELEE_RAW_SURFACELESS")!=NULL;
 int fd=surfaceless?-1:open("/dev/dri/card0",O_RDWR|O_CLOEXEC);
 struct gbm_device* gbm=surfaceless?NULL:gbm_create_device(fd);
 PFNEGLGETPLATFORMDISPLAYEXTPROC get=(void*)eglGetProcAddress("eglGetPlatformDisplayEXT");
 EGLDisplay d=get(surfaceless?EGL_PLATFORM_SURFACELESS_MESA:EGL_PLATFORM_GBM_KHR,gbm,NULL);int a,b,n;
 if(!eglInitialize(d,&a,&b))return 2;
 eglBindAPI(EGL_OPENGL_ES_API);
 EGLint attrs[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_SURFACE_TYPE,surfaceless?EGL_PBUFFER_BIT:EGL_WINDOW_BIT,EGL_NONE};EGLConfig cfg;
 if(!eglChooseConfig(d,attrs,&cfg,1,&n)||!n)return 2;
 EGLint ca[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};EGLContext c=eglCreateContext(d,cfg,EGL_NO_CONTEXT,ca);
 if(!eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,c))return 2;
 printf("GL=%s GPU=%s\n",glGetString(GL_VERSION),glGetString(GL_RENDERER));
 GLuint fbo,color,tex,vbo,ubo,vao;
 glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
 glGenTextures(1,&color);glBindTexture(GL_TEXTURE_2D,color);glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,256,256);
 glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
 if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return 2;
 glViewport(0,0,256,256);
 float vertices[]={-.09f,-.09f,.09f,-.09f,0,.09f};uint32_t words[1024]={0};memcpy(words,vertices,sizeof(vertices));
 glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);glTexStorage2D(GL_TEXTURE_2D,1,GL_R32UI,1024,1);
 glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1024,1,GL_RED_INTEGER,GL_UNSIGNED_INT,words);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);
 glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,0);glEnableVertexAttribArray(0);
 GLint alignment;glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT,&alignment);unsigned stride=(16+alignment-1)/alignment*alignment;
 char* data=calloc(64,stride);
 for(unsigned i=0;i<64;i++){float* v=(void*)(data+i*stride);v[0]=-.875f+.25f*(i%8);v[1]=-.875f+.25f*(i/8);}
 glGenBuffers(1,&ubo);glBindBuffer(GL_UNIFORM_BUFFER,ubo);glBufferData(GL_UNIFORM_BUFFER,64*stride,data,GL_STATIC_DRAW);
 GLuint separate[64];glGenBuffers(64,separate);
 for(unsigned i=0;i<64;i++){glBindBuffer(GL_UNIFORM_BUFFER,separate[i]);glBufferData(GL_UNIFORM_BUFFER,16,data+i*stride,GL_STATIC_DRAW);}
 const char* vs[6]={
 "#version 310 es\nprecision highp float;layout(location=0)in vec2 p;uniform vec4 offset;void main(){gl_Position=vec4(p+offset.xy,0,1);}",
 "#version 310 es\nprecision highp float;precision highp usampler2D;uniform usampler2D verts;uniform vec4 offset;void main(){int i=gl_VertexID*2;vec2 p=uintBitsToFloat(uvec2(texelFetch(verts,ivec2(i,0),0).r,texelFetch(verts,ivec2(i+1,0),0).r));gl_Position=vec4(p+offset.xy,0,1);}",
 "#version 310 es\nprecision highp float;precision highp usampler2D;uniform usampler2D verts;layout(std140,binding=0)uniform Params{vec4 offset;};void main(){int i=gl_VertexID*2;vec2 p=uintBitsToFloat(uvec2(texelFetch(verts,ivec2(i,0),0).r,texelFetch(verts,ivec2(i+1,0),0).r));gl_Position=vec4(p+offset.xy,0,1);}",
 "#version 310 es\nprecision highp float;layout(location=0)in vec2 p;layout(std140,binding=0)uniform Params{vec4 offset;};void main(){gl_Position=vec4(p+offset.xy,0,1);}"
 };
 unsigned char* pixels=malloc(256*256*4);int failed=0;
 vs[4]=vs[2];
 vs[5]="#version 310 es\nprecision highp float;precision highp usampler2D;uniform usampler2D verts;uniform uint drawindex;layout(std140,binding=0)uniform Params{vec4 offsets[64];};void main(){int i=gl_VertexID*2;vec2 p=uintBitsToFloat(uvec2(texelFetch(verts,ivec2(i,0),0).r,texelFetch(verts,ivec2(i+1,0),0).r));gl_Position=vec4(p+offsets[drawindex].xy,0,1);}";
 float packed[64*4];for(unsigned i=0;i<64;i++)memcpy(packed+i*4,data+i*stride,16);
 GLuint arrayBuffer;glGenBuffers(1,&arrayBuffer);glBindBuffer(GL_UNIFORM_BUFFER,arrayBuffer);glBufferData(GL_UNIFORM_BUFFER,sizeof(packed),packed,GL_STATIC_DRAW);
 for(unsigned mode=0;mode<6;mode++){
  GLuint program=glCreateProgram(),v=shader(GL_VERTEX_SHADER,vs[mode]),f=shader(GL_FRAGMENT_SHADER,"#version 310 es\nprecision highp float;out vec4 c;void main(){c=vec4(1,0,0,1);}");
  glAttachShader(program,v);glAttachShader(program,f);glLinkProgram(program);GLint linked;glGetProgramiv(program,GL_LINK_STATUS,&linked);if(!linked)return 2;glUseProgram(program);
  GLint loc=glGetUniformLocation(program,"offset");
  GLint drawloc=glGetUniformLocation(program,"drawindex");
  if(mode==5)glBindBufferBase(GL_UNIFORM_BUFFER,0,arrayBuffer);
  GLenum setup=glGetError();printf("setup mode=%u error=%x\n",mode,setup);if(setup)failed=1;
  for(unsigned barriers=0;barriers<2;barriers++){
   printf("begin mode=%u barriers=%u\n",mode,barriers);fflush(stdout);
   for(unsigned frame=0;frame<30;frame++){
    glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    for(unsigned draw=0;draw<64;draw++){
     if(mode==5)glUniform1ui(drawloc,draw);
     else if(mode==4)glBindBufferBase(GL_UNIFORM_BUFFER,0,separate[draw]);
     else if(mode>=2)glBindBufferRange(GL_UNIFORM_BUFFER,0,ubo,draw*stride,16);
     else glUniform4fv(loc,1,(float*)(data+draw*stride));
     glDrawArraysInstanced(GL_TRIANGLES,0,3,1);
     if(getenv("MELEE_RAW_REBIND_PROGRAM")){glUseProgram(0);glUseProgram(program);}
     if(barriers)glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
    }
    glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    unsigned lit=0;for(unsigned i=0;i<64;i++)lit+=pixels[((16+32*(i/8))*256+16+32*(i%8))*4]>200;
    GLenum error=glGetError();if(lit!=64||error){printf("FAIL mode=%u barriers=%u frame=%u lit=%u error=%x\n",mode,barriers,frame,lit,error);failed=1;break;}
   }
   printf("completed mode=%u barriers=%u\n",mode,barriers);fflush(stdout);
  }
  glDeleteProgram(program);glDeleteShader(v);glDeleteShader(f);
 }
 printf("%s direct GLES hazard control\n",failed?"FAIL":"PASS");
 eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(d,c);eglTerminate(d);if(gbm)gbm_device_destroy(gbm);if(fd>=0)close(fd);return failed;
}
