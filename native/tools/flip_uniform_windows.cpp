// Experimental uniform-window batching for the stock Flip GLES driver.
// No CPU readback: keep a larger UBO range stable and select each draw's data
// with a scalar uniform. Synchronize when changing the actual buffer window.
#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <regex>
#include <string>
#include <unordered_map>
using Proc=__eglMustCastToProperFunctionPointerType;
static Proc get(const char* n){static auto real=(Proc(*)(const char*))dlsym(RTLD_NEXT,"eglGetProcAddress");return real(n);}
static constexpr GLintptr Window=16384,Step=8192;
static GLuint program,buffer;
static GLintptr physicalOffset=-1,logicalOffset;
static GLsizeiptr physicalSize;
static bool pending;
static unsigned drawCount, barrierCount;
static std::unordered_map<GLuint,GLint> locations;
static void barrier(){if(pending){++barrierCount;((PFNGLMEMORYBARRIERPROC)get("glMemoryBarrier"))(GL_TEXTURE_FETCH_BARRIER_BIT);pending=false;}}
static GLint location(){
 if(!program)return -1;
 auto it=locations.find(program);if(it!=locations.end())return it->second;
 return locations[program]=((PFNGLGETUNIFORMLOCATIONPROC)get("glGetUniformLocation"))(program,"flip_uniform_base");
}
static void integers(GLenum n,GLint* v){if(n==GL_NUM_PROGRAM_BINARY_FORMATS){*v=0;return;}((PFNGLGETINTEGERVPROC)get("glGetIntegerv"))(n,v);}
static void source(GLuint shader,GLsizei count,const GLchar*const* strings,const GLint* lengths){
 std::string s;for(int i=0;i<count;i++)s.append(strings[i],lengths&&lengths[i]>=0?size_t(lengths[i]):strlen(strings[i]));
 static const std::regex block(R"(layout\(binding\s*=\s*0,\s*std140\)\s*uniform\s+(\w*ubuf_block_ubo)\s*\{\s*uvec4\s+inner\[(\d+)\];\s*\}\s*(\w+);)");
 std::smatch m;
 if(std::regex_search(s,m,block)){
  const unsigned vectors=unsigned(std::stoul(m[2]));
  if(vectors*16>Step){fprintf(stderr,"[uniform-window] block too large: %u\n",vectors);}
  else{
   const std::string instance=m[3];
   s.replace(m.position(),m.length(),"layout(binding=0,std140) uniform "+m[1].str()+" { uvec4 inner[1024]; } "+instance+";\nuniform highp uint flip_uniform_base;");
   const std::string access=instance+".inner[";
   for(size_t pos=0;(pos=s.find(access,pos))!=std::string::npos;){
    const size_t begin=pos+access.size();size_t end=begin;int depth=1;
    while(end<s.size()&&depth){if(s[end]=='[')depth++;if(s[end]==']')depth--;if(depth)end++;}
    if(depth)break;
    s.replace(begin,end-begin,"flip_uniform_base + ("+s.substr(begin,end-begin)+")");
    pos=end+strlen("flip_uniform_base + ()")+1;
   }
   fprintf(stderr,"[uniform-window] shader=%u original_vectors=%u\n",shader,vectors);
  }
 }
 const char* p=s.c_str();((PFNGLSHADERSOURCEPROC)get("glShaderSource"))(shader,1,&p,nullptr);
}
static void use(GLuint p){if(program!=p)barrier();program=p;((PFNGLUSEPROGRAMPROC)get("glUseProgram"))(p);}
static void del(GLuint p){locations.erase(p);((PFNGLDELETEPROGRAMPROC)get("glDeleteProgram"))(p);}
static void range(GLenum t,GLuint index,GLuint b,GLintptr off,GLsizeiptr size){
 if(t!=GL_UNIFORM_BUFFER||index!=0){((PFNGLBINDBUFFERRANGEPROC)get("glBindBufferRange"))(t,index,b,off,size);return;}
 logicalOffset=off;
 if(location()>=0&&b){
  GLint previous,total;
  ((PFNGLGETINTEGERVPROC)get("glGetIntegerv"))(GL_COPY_READ_BUFFER_BINDING,&previous);
  ((PFNGLBINDBUFFERPROC)get("glBindBuffer"))(GL_COPY_READ_BUFFER,b);
  ((PFNGLGETBUFFERPARAMETERIVPROC)get("glGetBufferParameteriv"))(GL_COPY_READ_BUFFER,GL_BUFFER_SIZE,&total);
  ((PFNGLBINDBUFFERPROC)get("glBindBuffer"))(GL_COPY_READ_BUFFER,previous);
  GLintptr window=off/Step*Step;
  if(window+Window>total)window=total-Window;
  if(window<0||off-window+size>Window){fprintf(stderr,"[uniform-window] cannot cover buffer range\n");std::abort();}
  off=window;size=Window;
 }
 if(buffer==b&&physicalOffset==off&&physicalSize==size){
  ((PFNGLBINDBUFFERPROC)get("glBindBuffer"))(GL_UNIFORM_BUFFER,b);
  return;
 }
 barrier();
 buffer=b;physicalOffset=off;physicalSize=size;
 ((PFNGLBINDBUFFERRANGEPROC)get("glBindBufferRange"))(t,index,b,off,size);
}
static void base(GLenum t,GLuint index,GLuint b){
 if(t==GL_UNIFORM_BUFFER){barrier();if(index==0){buffer=b;physicalOffset=-1;}}
 ((PFNGLBINDBUFFERBASEPROC)get("glBindBufferBase"))(t,index,b);
}
static void sub(GLenum t,GLintptr off,GLsizeiptr size,const void* p){
 barrier();
 ((PFNGLBUFFERSUBDATAPROC)get("glBufferSubData"))(t,off,size,p);
}
static void before(){if(++drawCount%1024==0){fprintf(stderr,"[uniform-window-stats] draws=1024 barriers=%u\n",barrierCount);barrierCount=0;}GLint loc=location();if(loc>=0)((PFNGLUNIFORM1UIPROC)get("glUniform1ui"))(loc,GLuint((logicalOffset-physicalOffset)/16));}
static void arrays(GLenum m,GLint first,GLsizei n,GLsizei i){before();((PFNGLDRAWARRAYSINSTANCEDPROC)get("glDrawArraysInstanced"))(m,first,n,i);pending=true;}
static void elements(GLenum m,GLsizei n,GLenum t,const void* p,GLsizei i){before();((PFNGLDRAWELEMENTSINSTANCEDPROC)get("glDrawElementsInstanced"))(m,n,t,p,i);pending=true;}
static void texture(GLenum target,GLuint id){barrier();((PFNGLBINDTEXTUREPROC)get("glBindTexture"))(target,id);}
static void sampler(GLuint unit,GLuint id){barrier();((PFNGLBINDSAMPLERPROC)get("glBindSampler"))(unit,id);}
static void copy(GLenum r,GLenum w,GLintptr ro,GLintptr wo,GLsizeiptr n){barrier();((PFNGLCOPYBUFFERSUBDATAPROC)get("glCopyBufferSubData"))(r,w,ro,wo,n);}
static void texSub(GLenum t,GLint l,GLint x,GLint y,GLsizei w,GLsizei h,GLenum f,GLenum type,const void* p){barrier();((PFNGLTEXSUBIMAGE2DPROC)get("glTexSubImage2D"))(t,l,x,y,w,h,f,type,p);}
extern "C" Proc eglGetProcAddress(const char* n){
#define HOOK(name,fn) if(!strcmp(n,name))return reinterpret_cast<Proc>(fn)
 HOOK("glBindTexture",texture);HOOK("glBindSampler",sampler);HOOK("glCopyBufferSubData",copy);HOOK("glTexSubImage2D",texSub);
 HOOK("glGetIntegerv",integers);HOOK("glShaderSource",source);HOOK("glUseProgram",use);HOOK("glDeleteProgram",del);HOOK("glBindBufferRange",range);HOOK("glBindBufferBase",base);HOOK("glBufferSubData",sub);HOOK("glDrawArraysInstanced",arrays);HOOK("glDrawElementsInstanced",elements);
 return get(n);
}
