// Experimental GLES interposer: replace GLSL uniform blocks with plain arrays.
// Kept outside the normal runtime; tests must use a fresh process and no binary cache.
#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <regex>
#include <string>
#include <vector>
#include <unordered_map>
#include <array>
using Proc=__eglMustCastToProperFunctionPointerType;
static Proc get(const char* n) {static auto real=(Proc(*)(const char*))dlsym(RTLD_NEXT,"eglGetProcAddress");return real(n);}
struct Block {std::string name;unsigned binding,count;};
static std::unordered_map<GLuint,std::vector<Block>> shaderBlocks,programBlocks;
struct Bound {GLuint buffer=0;GLintptr offset=0;GLsizeiptr size=0;};
static std::array<Bound,32> ubos;
static std::unordered_map<GLenum,GLuint> bound;
static std::unordered_map<GLuint,std::vector<unsigned char>> copies;
static GLuint currentProgram;
static void integers(GLenum name,GLint* value) {
 if(name==GL_NUM_PROGRAM_BINARY_FORMATS){*value=0;return;}
 ((PFNGLGETINTEGERVPROC)get("glGetIntegerv"))(name,value);
}
static void source(GLuint shader,GLsizei count,const GLchar*const* strings,const GLint* lengths) {
 std::string s;for(int i=0;i<count;i++)s.append(strings[i],lengths&&lengths[i]>=0?size_t(lengths[i]):strlen(strings[i]));
 static const std::regex block(R"(layout\(binding\s*=\s*(\d+),\s*std140\)\s*uniform\s+(\w+)\s*\{\s*uvec4\s+(\w+)\[(\d+)\];\s*\}\s*(\w+);)");
 std::smatch m;auto& blocks=shaderBlocks[shader];blocks.clear();
 while(std::regex_search(s,m,block)) {
  std::string name="flip_plain_"+m[2].str();
  blocks.push_back({name,unsigned(std::stoul(m[1])),unsigned(std::stoul(m[4]))});
  const auto member=m[5].str()+"."+m[3].str();
  s.replace(m.position(),m.length(),"uniform highp uvec4 "+name+"["+m[4].str()+"];");
  for(size_t pos=0;(pos=s.find(member,pos))!=std::string::npos;pos+=name.size())s.replace(pos,member.size(),name);
 }
 const char* ptr=s.c_str();((PFNGLSHADERSOURCEPROC)get("glShaderSource"))(shader,1,&ptr,nullptr);
 if(!blocks.empty())fprintf(stderr,"[plain-uniforms] shader=%u blocks=%zu\n",shader,blocks.size());
}
static void attach(GLuint program,GLuint shader){
 auto& p=programBlocks[program];auto& s=shaderBlocks[shader];p.insert(p.end(),s.begin(),s.end());
 ((PFNGLATTACHSHADERPROC)get("glAttachShader"))(program,shader);
}
static void delProgram(GLuint p){programBlocks.erase(p);((PFNGLDELETEPROGRAMPROC)get("glDeleteProgram"))(p);}
static void use(GLuint p){currentProgram=p;((PFNGLUSEPROGRAMPROC)get("glUseProgram"))(p);}
static void bind(GLenum target,GLuint b){bound[target]=b;((PFNGLBINDBUFFERPROC)get("glBindBuffer"))(target,b);}
static void range(GLenum target,GLuint index,GLuint b,GLintptr offset,GLsizeiptr size){
 bound[target]=b;
 if(target==GL_UNIFORM_BUFFER&&index<ubos.size())ubos[index]={b,offset,size};
 ((PFNGLBINDBUFFERRANGEPROC)get("glBindBufferRange"))(target,index,b,offset,size);
}
static void base(GLenum target,GLuint index,GLuint b){
 bound[target]=b;
 if(target==GL_UNIFORM_BUFFER&&index<ubos.size())ubos[index]={b,0,0};
 ((PFNGLBINDBUFFERBASEPROC)get("glBindBufferBase"))(target,index,b);
}
static void data(GLenum t,GLsizeiptr size,const void* p,GLenum usage){copies.erase(bound[t]);((PFNGLBUFFERDATAPROC)get("glBufferData"))(t,size,p,usage);}
static void sub(GLenum t,GLintptr off,GLsizeiptr size,const void* p){copies.erase(bound[t]);((PFNGLBUFFERSUBDATAPROC)get("glBufferSubData"))(t,off,size,p);}
static void copy(GLenum r,GLenum w,GLintptr ro,GLintptr wo,GLsizeiptr size){copies.erase(bound[w]);((PFNGLCOPYBUFFERSUBDATAPROC)get("glCopyBufferSubData"))(r,w,ro,wo,size);}
static void removeBuffers(GLsizei n,const GLuint* bs){for(int i=0;i<n;i++)copies.erase(bs[i]);((PFNGLDELETEBUFFERSPROC)get("glDeleteBuffers"))(n,bs);}
static void upload(){
 auto it=programBlocks.find(currentProgram);if(it==programBlocks.end())return;
 for(const auto& block:it->second){
  if(block.binding>=ubos.size())continue;const auto& u=ubos[block.binding];if(!u.buffer)continue;
  GLint loc=((PFNGLGETUNIFORMLOCATIONPROC)get("glGetUniformLocation"))(currentProgram,block.name.c_str());if(loc<0)continue;
  auto& bytes=copies[u.buffer];
  if(bytes.empty()){
   GLint previous=0,size=0;((PFNGLGETINTEGERVPROC)get("glGetIntegerv"))(GL_COPY_READ_BUFFER_BINDING,&previous);
   ((PFNGLBINDBUFFERPROC)get("glBindBuffer"))(GL_COPY_READ_BUFFER,u.buffer);
   ((PFNGLGETBUFFERPARAMETERIVPROC)get("glGetBufferParameteriv"))(GL_COPY_READ_BUFFER,GL_BUFFER_SIZE,&size);
   const void* p=((PFNGLMAPBUFFERRANGEPROC)get("glMapBufferRange"))(GL_COPY_READ_BUFFER,0,size,GL_MAP_READ_BIT);
   if(p){bytes.resize(size);memcpy(bytes.data(),p,size);((PFNGLUNMAPBUFFERPROC)get("glUnmapBuffer"))(GL_COPY_READ_BUFFER);}
   ((PFNGLBINDBUFFERPROC)get("glBindBuffer"))(GL_COPY_READ_BUFFER,previous);
  }
  if(u.offset<0||size_t(u.offset)+block.count*16>bytes.size()){fprintf(stderr,"[plain-uniforms] missing buffer=%u offset=%ld count=%u\n",u.buffer,long(u.offset),block.count);continue;}
  ((PFNGLUNIFORM4UIVPROC)get("glUniform4uiv"))(loc,block.count,reinterpret_cast<const GLuint*>(bytes.data()+u.offset));
 }
}
static void arrays(GLenum mode,GLint first,GLsizei count,GLsizei instances){upload();((PFNGLDRAWARRAYSINSTANCEDPROC)get("glDrawArraysInstanced"))(mode,first,count,instances);}
static void elements(GLenum mode,GLsizei count,GLenum type,const void* indices,GLsizei instances){upload();((PFNGLDRAWELEMENTSINSTANCEDPROC)get("glDrawElementsInstanced"))(mode,count,type,indices,instances);}
extern "C" Proc eglGetProcAddress(const char* n){
#define HOOK(name,fn) if(!strcmp(n,name))return reinterpret_cast<Proc>(fn)
 HOOK("glGetIntegerv",integers);HOOK("glShaderSource",source);HOOK("glAttachShader",attach);HOOK("glDeleteProgram",delProgram);HOOK("glUseProgram",use);
 HOOK("glBindBuffer",bind);HOOK("glBindBufferRange",range);HOOK("glBindBufferBase",base);HOOK("glBufferData",data);HOOK("glBufferSubData",sub);HOOK("glCopyBufferSubData",copy);HOOK("glDeleteBuffers",removeBuffers);
 HOOK("glDrawArraysInstanced",arrays);HOOK("glDrawElementsInstanced",elements);
 return get(n);
}
