#include "display.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <array>
#include <atomic>
#include <limits>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

bool MeleeFlipThreadedPresentEnabled();
bool MeleeFlipInPresentWorker();
void MeleeFlipInitPresenter();
void MeleeFlipShutdownPresenter();

namespace {
int fd = -1;
gbm_device* device;
gbm_surface* window;
EGLDisplay display = EGL_NO_DISPLAY;
drmModeCrtc* saved;
drmModeModeInfo mode;
uint32_t connector, crtc, previousFB;
gbm_bo* previousBO;
bool modeSet;
bool pendingFlip;
uint32_t retiredFB;
gbm_bo* retiredBO;
const bool asyncPresent = [] { const auto* s=std::getenv("MELEE_FLIP_ASYNC_PRESENT"); return s && !std::strcmp(s,"1"); }();
void wait_pending_flip();
PFNGLDRAWARRAYSINSTANCEDPROC drawArrays;
PFNGLDRAWELEMENTSINSTANCEDPROC drawElements;
PFNGLMEMORYBARRIERPROC memoryBarrier;
PFNGLUSEPROGRAMPROC realUseProgram;
PFNGLBINDBUFFERRANGEPROC realBindBufferRange;
PFNGLBINDBUFFERPROC realBindBuffer;
PFNGLBINDTEXTUREPROC realBindTexture;
PFNGLBINDSAMPLERPROC realBindSampler;
PFNGLBINDVERTEXARRAYPROC realBindVertexArray;
PFNGLVERTEXATTRIBPOINTERPROC realVertexAttribPointer;
PFNGLVERTEXATTRIBIPOINTERPROC realVertexAttribIPointer;
PFNGLUNIFORM4UIVPROC realUniform4uiv;
PFNGLACTIVETEXTUREPROC realActiveTexture;
PFNGLREADPIXELSPROC realReadPixels;
PFNGLMAPBUFFERRANGEPROC realMapBufferRange;
std::atomic<uint64_t> readPixelCalls{}, readPixelsCount{}, readMapCalls{}, readMapBytes{};
std::atomic<uint64_t> readPixelNs{}, readMapNs{};
PFNEGLSWAPBUFFERSPROC realSwapBuffers;
EGLBoolean swapBuffersUnpaced(EGLDisplay dpy, EGLSurface surface) {
    eglSwapInterval(dpy,0);
    return realSwapBuffers(dpy,surface);
}

// Optional CPU wall-time profiler. Driver waits are included in these timings;
// draw/barrier times are subsets of submit, not additional frame costs.
using ProfileClock = std::chrono::steady_clock;
const bool profiling = std::getenv("MELEE_FLIP_PROFILE") != nullptr;
// Diagnostic selection covers a 64-draw window; all calls outside the window
// and auxiliary submissions retain their barriers. Frame ordinals are not a
// production optimization: they change with scene and pipeline availability.
const char* barrierSelection = std::getenv("MELEE_FLIP_BARRIER_MASK");
uint64_t selectedBarriers = barrierSelection ? std::strtoull(barrierSelection, nullptr, 0) : UINT64_MAX;
unsigned barrierBase = [] {
    const char* value = std::getenv("MELEE_FLIP_BARRIER_BASE");
    return value ? static_cast<unsigned>(std::strtoul(value, nullptr, 0)) : 0;
}();
// Diagnostic global thinning: 0 removes all frame-draw barriers, N keeps
// every Nth barrier. Auxiliary submissions remain synchronized.
unsigned barrierEvery = [] {
    const char* value = std::getenv("MELEE_FLIP_BARRIER_EVERY");
    return value ? static_cast<unsigned>(std::strtoul(value, nullptr, 0)) : 1;
}();
const bool barrierTrace = std::getenv("MELEE_FLIP_BARRIER_TRACE") != nullptr;
bool submittingFrame;
bool traceThisFrame;
unsigned frameDraw, diagnosticFrame;
// Render-thread owned. Read a tiny atomically replaced file at frame boundaries;
// never change synchronization policy halfway through command submission.
const uint64_t startupMask = selectedBarriers;
const unsigned startupBase = barrierBase, startupEvery = barrierEvery;
const char* liveBarrierPath = [] {
    const char* value = std::getenv("MELEE_FLIP_BARRIER_CONFIG");
    return value ? value : "/tmp/melee-flip-barriers.conf";
}();
void reloadBarriers() {
    static auto nextPoll = ProfileClock::time_point{};
    const auto now = ProfileClock::now();
    if (now < nextPoll) return;
    nextPoll = now + std::chrono::milliseconds(250);
    traceThisFrame = unlink("/tmp/melee-flip-trace.request") == 0;
    uint64_t mask = startupMask;
    unsigned base = startupBase, every = startupEvery;
    if (auto* file = std::fopen(liveBarrierPath, "r")) {
        char input[256]{};
        const size_t length = std::fread(input, 1, sizeof(input) - 1, file);
        const bool failed = std::ferror(file) || !std::feof(file);
        std::fclose(file);
        char tokens[3][80]{}, extra;
        bool valid = !failed && length &&
            std::sscanf(input, "%79s %79s %79s %c", tokens[0], tokens[1], tokens[2], &extra) == 3;
        uint64_t values[3]{};
        for (unsigned i = 0; valid && i < 3; ++i) {
            char* end;
            errno = 0;
            values[i] = std::strtoull(tokens[i], &end, 0);
            valid = tokens[i][0] != '-' && end != tokens[i] && !*end && errno != ERANGE;
        }
        valid = valid && values[1] <= std::numeric_limits<unsigned>::max() &&
                values[2] <= std::numeric_limits<unsigned>::max();
        if (!valid) {
            static char lastInvalid[256]{};
            if (std::strcmp(input, lastInvalid)) {
                std::fprintf(stderr, "[flip-barrier-config] rejected malformed file; retaining current settings\n");
                std::memcpy(lastInvalid, input, sizeof(input));
            }
            return;
        }
        mask = values[0]; base = unsigned(values[1]); every = unsigned(values[2]);
    } else if (errno != ENOENT) return;
    if (mask == selectedBarriers && base == barrierBase && every == barrierEvery) return;
    // Drain preceding jobs before acknowledging a diagnostic policy change.
    // Otherwise faults from the previous mask can arrive in the next trial.
    glFinish();
    memoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
    selectedBarriers = mask; barrierBase = base; barrierEvery = every;
    std::fprintf(stderr, "[flip-barrier-config] frame=%u mask=0x%016llx base=%u every=%u\n",
                 diagnosticFrame, static_cast<unsigned long long>(mask), base, every);
}

void drawBarrier() {
    const unsigned ordinal = submittingFrame ? ++frameDraw : 0;
    const bool outside = !ordinal || ordinal <= barrierBase || ordinal - barrierBase > 64;
    const bool selected = outside || (selectedBarriers & (UINT64_C(1) << (ordinal - barrierBase - 1)));
    const bool apply = !ordinal || (selected && barrierEvery && ordinal % barrierEvery == 0);
    if (apply) memoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
    if (barrierTrace && submittingFrame) {
        std::fprintf(stderr, "[flip-barrier] frame=%u draw=%u applied=%d\n", diagnosticFrame, ordinal, apply);
    }
}
double drawMs, barrierMs, submitMs, swapMs, stateMs;
double drawCpuMs, submitCpuMs, swapCpuMs, submitCpuStart, swapCpuStart;
uint32_t useProgramCount, bindBufferRangeCount, bindBufferCount, bindTextureCount,
         bindSamplerCount, bindVertexArrayCount, vertexAttribCount, uniform4uivCount,
         activeTextureCount, skippedStateCount;
const bool cacheGlState = [] { const auto* s=std::getenv("MELEE_FLIP_GL_STATE_CACHE"); return s && std::strcmp(s,"0"); }();
constexpr GLuint UnknownGlName=~GLuint{0};
GLuint cachedProgram=UnknownGlName,cachedVao=UnknownGlName,cachedActiveTexture=UnknownGlName;
std::array<GLuint,32> cachedTextures,cachedSamplers;
double threadMillis() {
    timespec t{}; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1000000.0;
}

unsigned draws;
auto submitStart = ProfileClock::now(), swapStart = submitStart, lastPresent = submitStart;
double elapsed(ProfileClock::time_point start) {
    return std::chrono::duration<double, std::milli>(ProfileClock::now() - start).count();
}

// Count explicit CPU readback separately from uploads and GPU fence waits.
// Mapping callbacks can run outside submission, so these totals are atomic.
void readPixelsProfiled(GLint x, GLint y, GLsizei width, GLsizei height,
                        GLenum format, GLenum type, void* pixels) {
    const auto start = ProfileClock::now();
    realReadPixels(x,y,width,height,format,type,pixels);
    readPixelCalls.fetch_add(1,std::memory_order_relaxed);
    if (width > 0 && height > 0)
        readPixelsCount.fetch_add(uint64_t(width)*uint64_t(height),std::memory_order_relaxed);
    readPixelNs.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(
        ProfileClock::now()-start).count(),std::memory_order_relaxed);
}
void* mapBufferRangeProfiled(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) {
    if (!(access & GL_MAP_READ_BIT)) return realMapBufferRange(target,offset,length,access);
    const auto start = ProfileClock::now();
    void* result = realMapBufferRange(target,offset,length,access);
    readMapCalls.fetch_add(1,std::memory_order_relaxed);
    if (length > 0) readMapBytes.fetch_add(uint64_t(length),std::memory_order_relaxed);
    readMapNs.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(
        ProfileClock::now()-start).count(),std::memory_order_relaxed);
    return result;
}

// The stock g13p0 Mali driver loses geometry without a texture-fetch barrier
// between draws. Both multi-draw probe paths reproduce this driver failure.
void traceDrawState(GLsizei count) {
    if (!traceThisFrame) return;
    static std::array<GLuint,8> describedPrograms{};
    static unsigned describedCount=0;
    GLint program = 0, framebuffer = 0, indexBuffer = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &indexBuffer);
    std::fprintf(stderr, "[flip-state] frame=%u draw=%u program=%d fbo=%d index=%d count=%d",
                 diagnosticFrame, frameDraw + 1, program, framebuffer, indexBuffer, count);
    for (unsigned i = 0; i < 8; ++i) {
        GLint buffer = 0;
        GLint64 start = 0, size = 0;
        glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, i, &buffer);
        if (!buffer) continue;
        glGetInteger64i_v(GL_UNIFORM_BUFFER_START, i, &start);
        glGetInteger64i_v(GL_UNIFORM_BUFFER_SIZE, i, &size);
        std::fprintf(stderr, " ubo%u=%d:%lld:%lld", i, buffer,
                     static_cast<long long>(start), static_cast<long long>(size));
    }
    std::fprintf(stderr, "\n");
    bool known=false;
    for(unsigned i=0;i<describedCount;++i) known|=describedPrograms[i]==GLuint(program);
    if(!known&&program&&describedCount<describedPrograms.size()) {
        describedPrograms[describedCount++]=GLuint(program);
        GLint blocks=0,uniforms=0;
        glGetProgramiv(program,GL_ACTIVE_UNIFORM_BLOCKS,&blocks);
        glGetProgramiv(program,GL_ACTIVE_UNIFORMS,&uniforms);
        std::fprintf(stderr,"[flip-program] id=%d blocks=%d uniforms=%d\n",program,blocks,uniforms);
        for(GLint i=0;i<blocks;++i) {
            char name[160]{};GLsizei length=0;GLint binding=0,size=0;
            glGetActiveUniformBlockName(program,GLuint(i),sizeof(name),&length,name);
            glGetActiveUniformBlockiv(program,GLuint(i),GL_UNIFORM_BLOCK_BINDING,&binding);
            glGetActiveUniformBlockiv(program,GLuint(i),GL_UNIFORM_BLOCK_DATA_SIZE,&size);
            std::fprintf(stderr,"[flip-block] program=%d index=%d binding=%d size=%d name=%.*s\n",
                         program,i,binding,size,int(length),name);
        }
        for(GLint i=0;i<uniforms;++i) {
            char name[160]{};GLsizei length=0,count=0;GLenum type=0;
            glGetActiveUniform(program,GLuint(i),sizeof(name),&length,&count,&type,name);
            const GLint location=glGetUniformLocation(program,name);
            GLint value=-1;if(location>=0)glGetUniformiv(program,location,&value);
            std::fprintf(stderr,"[flip-uniform] program=%d index=%d location=%d type=0x%x count=%d value=%d name=%.*s\n",
                         program,i,location,type,count,value,int(length),name);
        }
    }
}
void drawArraysVisible(GLenum mode, GLint first, GLsizei count, GLsizei instances) {
    traceDrawState(count);
    const auto start = profiling ? ProfileClock::now() : ProfileClock::time_point{};
    if (barrierTrace && submittingFrame) std::fprintf(stderr,
        "[flip-draw] frame=%u draw=%u arrays count=%d instances=%d\n", diagnosticFrame, frameDraw + 1, count, instances);
    const double cpuStart = profiling ? threadMillis() : 0;
    drawArrays(mode, first, count, instances);
    if (profiling) { drawMs += elapsed(start); drawCpuMs += threadMillis() - cpuStart; ++draws; }
    const auto barrierStart = profiling ? ProfileClock::now() : ProfileClock::time_point{};
    drawBarrier();
    if (profiling) barrierMs += elapsed(barrierStart);
}
void drawElementsVisible(GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei instances) {
    traceDrawState(count);
    const auto start = profiling ? ProfileClock::now() : ProfileClock::time_point{};
    if (barrierTrace && submittingFrame) std::fprintf(stderr,
        "[flip-draw] frame=%u draw=%u elements count=%d instances=%d\n", diagnosticFrame, frameDraw + 1, count, instances);
    const double cpuStart = profiling ? threadMillis() : 0;
    drawElements(mode, count, type, indices, instances);
    if (profiling) { drawMs += elapsed(start); drawCpuMs += threadMillis() - cpuStart; ++draws; }
    const auto barrierStart = profiling ? ProfileClock::now() : ProfileClock::time_point{};
    drawBarrier();
    if (profiling) barrierMs += elapsed(barrierStart);
}

#define FLIP_PROFILE_GL_VOID(name, pointer, parameters, arguments, counter) \
void name parameters { \
    const auto start = profiling && submittingFrame ? ProfileClock::now() : ProfileClock::time_point{}; \
    pointer arguments; \
    if (profiling && submittingFrame) { stateMs += elapsed(start); ++counter; } \
}
void useProgramVisible(GLuint program) {
    const auto start=profiling&&submittingFrame?ProfileClock::now():ProfileClock::time_point{};
    if(cacheGlState&&cachedProgram==program) ++skippedStateCount;
    else {realUseProgram(program);cachedProgram=program;}
    if(profiling&&submittingFrame){stateMs+=elapsed(start);++useProgramCount;}
}
FLIP_PROFILE_GL_VOID(bindBufferRangeVisible, realBindBufferRange,
    (GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size),
    (target,index,buffer,offset,size), bindBufferRangeCount)
FLIP_PROFILE_GL_VOID(bindBufferVisible, realBindBuffer, (GLenum target, GLuint buffer),
    (target,buffer), bindBufferCount)
void activeTextureVisible(GLenum texture) {
    const auto start=profiling&&submittingFrame?ProfileClock::now():ProfileClock::time_point{};
    const GLuint unit=texture-GL_TEXTURE0;
    if(cacheGlState&&cachedActiveTexture==unit) ++skippedStateCount;
    else {realActiveTexture(texture);cachedActiveTexture=unit;}
    if(profiling&&submittingFrame){stateMs+=elapsed(start);++activeTextureCount;}
}
void bindTextureVisible(GLenum target,GLuint texture) {
    const auto start=profiling&&submittingFrame?ProfileClock::now():ProfileClock::time_point{};
    const GLuint unit=cachedActiveTexture;
    if(cacheGlState&&target==GL_TEXTURE_2D&&unit<cachedTextures.size()&&cachedTextures[unit]==texture) ++skippedStateCount;
    else {realBindTexture(target,texture);if(target==GL_TEXTURE_2D&&unit<cachedTextures.size())cachedTextures[unit]=texture;}
    if(profiling&&submittingFrame){stateMs+=elapsed(start);++bindTextureCount;}
}
void bindSamplerVisible(GLuint unit,GLuint sampler) {
    const auto start=profiling&&submittingFrame?ProfileClock::now():ProfileClock::time_point{};
    if(cacheGlState&&unit<cachedSamplers.size()&&cachedSamplers[unit]==sampler) ++skippedStateCount;
    else {realBindSampler(unit,sampler);if(unit<cachedSamplers.size())cachedSamplers[unit]=sampler;}
    if(profiling&&submittingFrame){stateMs+=elapsed(start);++bindSamplerCount;}
}
void bindVertexArrayVisible(GLuint array) {
    const auto start=profiling&&submittingFrame?ProfileClock::now():ProfileClock::time_point{};
    if(cacheGlState&&cachedVao==array) ++skippedStateCount;
    else {realBindVertexArray(array);cachedVao=array;}
    if(profiling&&submittingFrame){stateMs+=elapsed(start);++bindVertexArrayCount;}
}
FLIP_PROFILE_GL_VOID(vertexAttribPointerVisible, realVertexAttribPointer,
    (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer),
    (index,size,type,normalized,stride,pointer), vertexAttribCount)
FLIP_PROFILE_GL_VOID(vertexAttribIPointerVisible, realVertexAttribIPointer,
    (GLuint index, GLint size, GLenum type, GLsizei stride, const void* pointer),
    (index,size,type,stride,pointer), vertexAttribCount)
FLIP_PROFILE_GL_VOID(uniform4uivVisible, realUniform4uiv,
    (GLint location, GLsizei count, const GLuint* value), (location,count,value), uniform4uivCount)
#undef FLIP_PROFILE_GL_VOID

[[noreturn]] void fail(const char* message) {
    std::fprintf(stderr, "[flip-display] %s (errno=%d, EGL=0x%x)\n", message, errno, eglGetError());
    std::exit(1);
}
void cleanup() {
    MeleeFlipShutdownPresenter();
    if (pendingFlip) wait_pending_flip();
    if (retiredFB) drmModeRmFB(fd, retiredFB);
    if (retiredBO) gbm_surface_release_buffer(window, retiredBO);
    if (modeSet && saved) {
        drmModeSetCrtc(fd, saved->crtc_id, saved->buffer_id, saved->x, saved->y,
                      &connector, 1, &saved->mode);
    }
    if (previousFB) drmModeRmFB(fd, previousFB);
    if (previousBO) gbm_surface_release_buffer(window, previousBO);
    if (display != EGL_NO_DISPLAY) eglTerminate(display);
    if (window) gbm_surface_destroy(window);
    if (device) gbm_device_destroy(device);
    if (saved) drmModeFreeCrtc(saved);
    if (fd >= 0) close(fd);
}
void flipped(int, unsigned, unsigned, unsigned, void* data) {
    *static_cast<bool*>(data) = false;
}
void wait_pending_flip() {
    drmEventContext events{}; events.version=2; events.page_flip_handler=flipped;
    while (pendingFlip) {
        pollfd pfd{fd,POLLIN,0};
        const int result=poll(&pfd,1,2000);
        if (result<0 && errno==EINTR) continue;
        if (result<=0 || !(pfd.revents&POLLIN) || drmHandleEvent(fd,&events)) fail("Page flip wait failed");
    }
}
}

void MeleeFlipInitDisplay() {
    // The stock Flip firmware exposes display scanout on card0. Select a
    // connected panel and its active encoder rather than hardcoding object IDs.
    const char* node = std::getenv("MELEE_DRM_DEVICE");
    fd = open(node ? node : "/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) fail("Cannot open DRM device");
    std::atexit(cleanup);
    auto* resources = drmModeGetResources(fd);
    if (!resources) fail("Cannot enumerate display resources");
    for (int i = 0; i < resources->count_connectors && !connector; ++i) {
        auto* candidate = drmModeGetConnector(fd, resources->connectors[i]);
        if (!candidate) continue;
        if (candidate->connection == DRM_MODE_CONNECTED && candidate->count_modes) {
            auto* encoder = drmModeGetEncoder(fd, candidate->encoder_id);
            if (encoder && encoder->crtc_id) {
                connector = candidate->connector_id;
                crtc = encoder->crtc_id;
                saved = drmModeGetCrtc(fd, crtc);
                mode = saved && saved->mode_valid ? saved->mode : candidate->modes[0];
            }
            if (encoder) drmModeFreeEncoder(encoder);
        }
        drmModeFreeConnector(candidate);
    }
    drmModeFreeResources(resources);
    if (!connector) fail("No active connected display");
    if (mode.hdisplay != 640 || mode.vdisplay != 480) fail("Expected the Flip's 640x480 panel");
    device = gbm_create_device(fd);
    if (!device) fail("Cannot create GBM device");
    window = gbm_surface_create(device, 640, 480, GBM_FORMAT_ARGB8888,
                                GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!window) fail("Cannot create GBM surface");
    auto getDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (!getDisplay) fail("EGL platform display extension unavailable");
    display = getDisplay(EGL_PLATFORM_GBM_KHR, device, nullptr);
    EGLint major, minor;
    if (!eglInitialize(display, &major, &minor)) fail("Cannot initialize EGL display");
    std::fprintf(stderr, "[flip-display] EGL %d.%d, DRM connector %u, CRTC %u, 640x480\n", major, minor, connector, crtc);
    MeleeFlipInitPresenter();
}
extern "C" void* MeleeFlipNativeWindow() { return window; }
extern "C" void* MeleeFlipEGLDisplay() { return display; }
extern "C" __eglMustCastToProperFunctionPointerType MeleeFlipEGLProc(const char* name) {
    const auto proc = eglGetProcAddress(name);
    if (!std::strcmp(name,"eglSwapBuffers") && std::getenv("MELEE_FLIP_EGL_INTERVAL_ZERO")) {
        realSwapBuffers=reinterpret_cast<PFNEGLSWAPBUFFERSPROC>(proc);
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(swapBuffersUnpaced);
    }
    if (!std::strcmp(name, "glDrawArraysInstanced")) {
        drawArrays = reinterpret_cast<PFNGLDRAWARRAYSINSTANCEDPROC>(proc);
        memoryBarrier = reinterpret_cast<PFNGLMEMORYBARRIERPROC>(eglGetProcAddress("glMemoryBarrier"));
        if (!drawArrays || !memoryBarrier) fail("Required GLES draw functions unavailable");
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(drawArraysVisible);
    }
    if (!std::strcmp(name, "glDrawElementsInstanced")) {
        drawElements = reinterpret_cast<PFNGLDRAWELEMENTSINSTANCEDPROC>(proc);
        memoryBarrier = reinterpret_cast<PFNGLMEMORYBARRIERPROC>(eglGetProcAddress("glMemoryBarrier"));
        if (!drawElements || !memoryBarrier) fail("Required GLES indexed draw functions unavailable");
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(drawElementsVisible);
    }
#define FLIP_INTERCEPT_GL(procName, pointer, wrapper, type) \
    if (!std::strcmp(name, procName)) { \
        pointer = reinterpret_cast<type>(proc); \
        if (!pointer) fail("Required GLES profiling function unavailable"); \
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(wrapper); \
    }
    if (profiling) {
        FLIP_INTERCEPT_GL("glReadPixels", realReadPixels, readPixelsProfiled, PFNGLREADPIXELSPROC)
        FLIP_INTERCEPT_GL("glMapBufferRange", realMapBufferRange, mapBufferRangeProfiled, PFNGLMAPBUFFERRANGEPROC)
    }
    FLIP_INTERCEPT_GL("glUseProgram", realUseProgram, useProgramVisible, PFNGLUSEPROGRAMPROC)
    FLIP_INTERCEPT_GL("glBindBufferRange", realBindBufferRange, bindBufferRangeVisible, PFNGLBINDBUFFERRANGEPROC)
    FLIP_INTERCEPT_GL("glBindBuffer", realBindBuffer, bindBufferVisible, PFNGLBINDBUFFERPROC)
    FLIP_INTERCEPT_GL("glBindTexture", realBindTexture, bindTextureVisible, PFNGLBINDTEXTUREPROC)
    FLIP_INTERCEPT_GL("glBindSampler", realBindSampler, bindSamplerVisible, PFNGLBINDSAMPLERPROC)
    FLIP_INTERCEPT_GL("glBindVertexArray", realBindVertexArray, bindVertexArrayVisible, PFNGLBINDVERTEXARRAYPROC)
    FLIP_INTERCEPT_GL("glVertexAttribPointer", realVertexAttribPointer, vertexAttribPointerVisible, PFNGLVERTEXATTRIBPOINTERPROC)
    FLIP_INTERCEPT_GL("glVertexAttribIPointer", realVertexAttribIPointer, vertexAttribIPointerVisible, PFNGLVERTEXATTRIBIPOINTERPROC)
    FLIP_INTERCEPT_GL("glUniform4uiv", realUniform4uiv, uniform4uivVisible, PFNGLUNIFORM4UIVPROC)
    FLIP_INTERCEPT_GL("glActiveTexture", realActiveTexture, activeTextureVisible, PFNGLACTIVETEXTUREPROC)
#undef FLIP_INTERCEPT_GL
    return proc;
}
extern "C" void MeleeFlipProfileSubmit(bool begin) {
    submittingFrame = begin;
    if (begin) {
        frameDraw = 0; ++diagnosticFrame; traceThisFrame = false; reloadBarriers();
        cachedProgram=cachedVao=cachedActiveTexture=UnknownGlName;
        cachedTextures.fill(UnknownGlName);cachedSamplers.fill(UnknownGlName);
    }
    if (!profiling) return;
    if (begin) { submitStart = ProfileClock::now(); submitCpuStart = threadMillis(); }
    else {
        submitMs += elapsed(submitStart); submitCpuMs += threadMillis() - submitCpuStart;
        if (diagnosticFrame % 120 == 0) {
            std::fprintf(stderr,"[flip-readback] frame=%u pixel_calls=%llu pixels=%llu read_maps=%llu map_bytes=%llu pixel_ms=%.3f map_ms=%.3f cumulative=1\n",
                diagnosticFrame,
                static_cast<unsigned long long>(readPixelCalls.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(readPixelsCount.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(readMapCalls.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(readMapBytes.load(std::memory_order_relaxed)),
                readPixelNs.load(std::memory_order_relaxed)/1e6,readMapNs.load(std::memory_order_relaxed)/1e6);
        }
        if (MeleeFlipThreadedPresentEnabled()) {
            std::fprintf(stderr,"[flip-thread-submit] submit_ms=%.3f cpu_ms=%.3f\n",submitMs,submitCpuMs);
            submitMs=submitCpuMs=drawMs=drawCpuMs=barrierMs=stateMs=0;
            draws=useProgramCount=bindBufferRangeCount=bindBufferCount=bindTextureCount=0;
            bindSamplerCount=bindVertexArrayCount=vertexAttribCount=uniform4uivCount=0;
            activeTextureCount=skippedStateCount=0;
        }
    }
}
extern "C" void MeleeFlipProfileSwap(bool begin) {
    if (MeleeFlipThreadedPresentEnabled()) return;
    if (!profiling) return;
    if (begin) { swapStart = ProfileClock::now(); swapCpuStart = threadMillis(); }
    else { swapMs += elapsed(swapStart); swapCpuMs += threadMillis() - swapCpuStart; }
}
extern "C" void MeleeFlipPresent() {
    if (MeleeFlipThreadedPresentEnabled() && !MeleeFlipInPresentWorker()) return;
    const bool profileHere = profiling && !MeleeFlipThreadedPresentEnabled();
    const auto presentStart = profileHere ? ProfileClock::now() : ProfileClock::time_point{};
    if (asyncPresent) {
        wait_pending_flip();
        if (retiredFB) drmModeRmFB(fd,retiredFB);
        if (retiredBO) gbm_surface_release_buffer(window,retiredBO);
        retiredFB=0; retiredBO=nullptr;
    }
    auto* bo = gbm_surface_lock_front_buffer(window);
    if (!bo) fail("Cannot lock rendered frame");
    const double lockMs = profileHere ? elapsed(presentStart) : 0;
    uint32_t fb;
    if (drmModeAddFB(fd, 640, 480, 32, 32, gbm_bo_get_stride(bo), gbm_bo_get_handle(bo).u32, &fb))
        fail("Cannot register framebuffer");
    if (!modeSet) {
        if (drmModeSetCrtc(fd, crtc, fb, 0, 0, &connector, 1, &mode)) fail("Cannot acquire display scanout");
        modeSet = true;
    } else {
        pendingFlip = true;
        if (drmModePageFlip(fd, crtc, fb, DRM_MODE_PAGE_FLIP_EVENT, &pendingFlip)) fail("Page flip failed");
        if (!asyncPresent) wait_pending_flip();
    }
    if (asyncPresent) {
        retiredFB=previousFB; retiredBO=previousBO;
    } else {
        if (previousFB) drmModeRmFB(fd, previousFB);
        if (previousBO) gbm_surface_release_buffer(window, previousBO);
    }
    previousBO = bo;
    if (profileHere) {
        std::fprintf(stderr, "[flip-profile] frame_ms=%.3f submit_ms=%.3f draw_ms=%.3f barrier_ms=%.3f swap_ms=%.3f lock_ms=%.3f drm_ms=%.3f draws=%u\n",
                     elapsed(lastPresent), submitMs, drawMs, barrierMs, swapMs, lockMs,
                     elapsed(presentStart) - lockMs, draws);
        std::fprintf(stderr, "[flip-cpu] submit_ms=%.3f draw_ms=%.3f swap_ms=%.3f\n", submitCpuMs, drawCpuMs, swapCpuMs);
        std::fprintf(stderr, "[flip-gl-state] wall_ms=%.3f use=%u range=%u buffer=%u active=%u texture=%u sampler=%u vao=%u attrib=%u immediate=%u skipped=%u\n",
                     stateMs,useProgramCount,bindBufferRangeCount,bindBufferCount,activeTextureCount,
                     bindTextureCount,bindSamplerCount,bindVertexArrayCount,vertexAttribCount,uniform4uivCount,
                     skippedStateCount);
        submitCpuMs = drawCpuMs = swapCpuMs = 0;
        stateMs = 0;
        useProgramCount = bindBufferRangeCount = bindBufferCount = bindTextureCount = 0;
        bindSamplerCount = bindVertexArrayCount = vertexAttribCount = uniform4uivCount = 0;
        activeTextureCount=skippedStateCount=0;
        lastPresent = ProfileClock::now();
        drawMs = barrierMs = submitMs = swapMs = 0;
        draws = 0;
    }
    previousFB = fb;
}
