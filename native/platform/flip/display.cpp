#include "display.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <SDL3/SDL.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <array>
#include <atomic>
#include <limits>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <dlfcn.h>

// GBM is resolved at run time from the device's driver stack (Mesa's libgbm, or the libmali hook
// on Rockchip BSP firmware). Linking the build SDK's libgbm would pin the binary to libmali.
namespace gbm_runtime {
struct Api {
    gbm_device* (*create_device)(int);
    void (*device_destroy)(gbm_device*);
    gbm_surface* (*surface_create)(gbm_device*, uint32_t, uint32_t, uint32_t, uint32_t);
    void (*surface_destroy)(gbm_surface*);
    gbm_bo* (*surface_lock_front_buffer)(gbm_surface*);
    void (*surface_release_buffer)(gbm_surface*, gbm_bo*);
    uint32_t (*bo_get_stride)(gbm_bo*);
    gbm_bo_handle (*bo_get_handle)(gbm_bo*);
};
const Api& api() {
    static const Api table = [] {
        Api t{};
        void* lib = dlopen("libgbm.so.1", RTLD_NOW | RTLD_GLOBAL);
        if (!lib) {
            std::fprintf(stderr, "[flip-display] libgbm.so.1 unavailable: %s\n", dlerror());
            return t;
        }
        const auto bind = [lib](auto& fn, const char* name) { fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(lib, name)); };
        bind(t.create_device, "gbm_create_device");
        bind(t.device_destroy, "gbm_device_destroy");
        bind(t.surface_create, "gbm_surface_create");
        bind(t.surface_destroy, "gbm_surface_destroy");
        bind(t.surface_lock_front_buffer, "gbm_surface_lock_front_buffer");
        bind(t.surface_release_buffer, "gbm_surface_release_buffer");
        bind(t.bo_get_stride, "gbm_bo_get_stride");
        bind(t.bo_get_handle, "gbm_bo_get_handle");
        // Every object comes from the two constructors, so leaving them null disables the rest.
        if (!t.device_destroy || !t.surface_destroy || !t.surface_lock_front_buffer || !t.surface_release_buffer ||
            !t.bo_get_stride || !t.bo_get_handle)
            t.create_device = nullptr, t.surface_create = nullptr;
        return t;
    }();
    return table;
}
} // namespace gbm_runtime
#define gbm_create_device(fd) (gbm_runtime::api().create_device ? gbm_runtime::api().create_device(fd) : nullptr)
#define gbm_surface_create(...) (gbm_runtime::api().surface_create ? gbm_runtime::api().surface_create(__VA_ARGS__) : nullptr)
#define gbm_device_destroy(...) gbm_runtime::api().device_destroy(__VA_ARGS__)
#define gbm_surface_destroy(...) gbm_runtime::api().surface_destroy(__VA_ARGS__)
#define gbm_surface_lock_front_buffer(...) gbm_runtime::api().surface_lock_front_buffer(__VA_ARGS__)
#define gbm_surface_release_buffer(...) gbm_runtime::api().surface_release_buffer(__VA_ARGS__)
#define gbm_bo_get_stride(...) gbm_runtime::api().bo_get_stride(__VA_ARGS__)
#define gbm_bo_get_handle(...) gbm_runtime::api().bo_get_handle(__VA_ARGS__)

bool MeleeFlipThreadedPresentEnabled();
bool MeleeFlipInPresentWorker();
void MeleeFlipInitPresenter();
void MeleeFlipShutdownPresenter();

namespace {
// Display backend. The SDL/KMSDRM path (default on every device) lets the CFW's SDL own the
// display (DRM master handoff, console release, page flips) and presents our rendered frames
// into SDL's window surface. MELEE_FLIP_DISPLAY=drm keeps the legacy direct DRM/GBM path below
// as a per-device safety fallback (e.g. the Miyoo Flip, which our own modeset drives fine).
enum class DisplayBackend { Drm, Sdl };
DisplayBackend backend = [] {
    const char* v = std::getenv("MELEE_FLIP_DISPLAY");
    return (v && !std::strcmp(v, "drm")) ? DisplayBackend::Drm : DisplayBackend::Sdl;
}();

// SDL-owned display state (backend == Sdl).
SDL_Window* sdlWindow = nullptr;
SDL_GLContext sdlContext = nullptr;
EGLSurface sdlSurface = EGL_NO_SURFACE; // SDL's window EGL surface; the present worker swaps it.
gbm_device* sdlGbm = nullptr;           // borrowed from SDL (SDL owns/destroys it), not ours.
// Wayland: Dawn's scratch native window is a wl_egl_window on a hidden SDL window's wl_surface.
SDL_Window* dawnHiddenWindow = nullptr;
void* dawnWlWindow = nullptr;
void (*wlEglWindowDestroy)(void*) = nullptr;
gbm_surface* dawnWindow = nullptr;      // scratch native window Dawn's swapchain wraps (never presented).

int fd = -1;
gbm_device* device;
gbm_surface* window;
EGLDisplay display = EGL_NO_DISPLAY;
drmModeCrtc* saved;
drmModeModeInfo mode;
// Scanout size = the connector mode's size. Aurora renders at this size (runtime_main
// passes it as the window size); on the Flip's 640x480 panel that is the game's native
// resolution, on other panels the game is rendered at the panel size. No rotation.
uint32_t displayWidth = 640, displayHeight = 480;   // panel/scanout (DRM mode + GBM surface)
uint32_t renderWidth = 640, renderHeight = 480;     // what Aurora renders (GC native 4:3); scaled to the panel at present
int displayRotation = [] { const char* v = std::getenv("MELEE_FLIP_ROTATE"); return v ? std::atoi(v) : 0; }();
uint32_t connector, crtc, previousFB;
gbm_bo* previousBO;
bool modeSet;
bool pendingFlip;
uint32_t retiredFB;
gbm_bo* retiredBO;
const bool asyncPresent = [] { const auto* s=std::getenv("MELEE_FLIP_ASYNC_PRESENT"); return !s || std::strcmp(s,"0"); }();
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

[[noreturn]] void failSdl(const char* message) {
    std::fprintf(stderr, "[flip-display] %s (SDL: %s, EGL=0x%x)\n", message, SDL_GetError(), eglGetError());
    std::exit(1);
}
void cleanupSdl() {
    MeleeFlipShutdownPresenter();
    if (dawnWindow) gbm_surface_destroy(dawnWindow);
    if (dawnWlWindow) wlEglWindowDestroy(dawnWlWindow);
    if (dawnHiddenWindow) SDL_DestroyWindow(dawnHiddenWindow);
    if (sdlContext) SDL_GL_DestroyContext(sdlContext);
    if (sdlWindow) SDL_DestroyWindow(sdlWindow);   // releases DRM master and restores the console
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
// Let SDL/KMSDRM own the display and present into its window surface. SDL performs the DRM master
// handoff, console (fbcon/VT) release and the page flips; we render with Dawn's GLES backend on
// SDL's EGL display and blit+swap SDL's surface from the present worker.
// Bring up SDL video. The CFW's environment may name a driver (ROCKNIX exports SDL_VIDEODRIVER=wayland
// system-wide); honour it, and when it is unavailable fall back to SDL's own probe order. When nothing
// works, log what this build offers and why SDL rejected each driver, so a tester's log.txt explains
// the failure (drivers built in, /dev/dri nodes, SDL's own video-category debug output).
bool initSdlVideo() {
    // The SDL3-over-SDL2 shim's video driver is named "sdl2", and the launcher selects it through
    // SDL_VIDEODRIVER. The CFW's SDL2 inside the shim reads that same variable and rejects the name
    // ("sdl2 not available") unless the launcher also set SDL3SHIM_SDL2_VIDEODRIVER, which the shim
    // copies over it. Keep the choice as an SDL3 hint and drop the variable, so the inner SDL2 picks
    // its own default driver (KMSDRM, fbdev, ...) on CFWs that name none.
    const char* fromEnv = std::getenv("SDL_VIDEODRIVER");
    const char* wanted = SDL_GetHint(SDL_HINT_VIDEO_DRIVER);
    if ((fromEnv && !std::strcmp(fromEnv, "sdl2")) || (wanted && !std::strcmp(wanted, "sdl2"))) {
        SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "sdl2", SDL_HINT_OVERRIDE);
        unsetenv("SDL_VIDEODRIVER");
        unsetenv(SDL_HINT_VIDEO_DRIVER);
    }
    if (SDL_InitSubSystem(SDL_INIT_VIDEO)) return true;
    if (const char* wanted = SDL_GetHint(SDL_HINT_VIDEO_DRIVER); wanted && *wanted) {
        std::fprintf(stderr, "[flip-display] video driver '%s' from the environment is unavailable (%s); trying the others\n",
                     wanted, SDL_GetError());
        unsetenv("SDL_VIDEODRIVER");
        unsetenv(SDL_HINT_VIDEO_DRIVER);
        SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "", SDL_HINT_OVERRIDE);
        if (SDL_InitSubSystem(SDL_INIT_VIDEO)) return true;
    }
    std::fprintf(stderr, "[flip-display] SDL video failed: %s\n", SDL_GetError());
    std::fprintf(stderr, "[flip-display] video drivers in this build:");
    for (int i = 0; i < SDL_GetNumVideoDrivers(); ++i) std::fprintf(stderr, " %s", SDL_GetVideoDriver(i));
    std::fprintf(stderr, "\n[flip-display] WAYLAND_DISPLAY=%s XDG_RUNTIME_DIR=%s\n",
                 std::getenv("WAYLAND_DISPLAY") ? std::getenv("WAYLAND_DISPLAY") : "(unset)",
                 std::getenv("XDG_RUNTIME_DIR") ? std::getenv("XDG_RUNTIME_DIR") : "(unset)");
    if (DIR* dri = opendir("/dev/dri")) {
        std::fprintf(stderr, "[flip-display] /dev/dri:");
        while (const dirent* entry = readdir(dri)) {
            if (entry->d_name[0] != '.') std::fprintf(stderr, " %s", entry->d_name);
        }
        std::fprintf(stderr, "\n");
        closedir(dri);
    } else {
        std::fprintf(stderr, "[flip-display] /dev/dri: cannot open (errno=%d)\n", errno);
    }
    for (const char* name : {"libdrm.so.2", "libgbm.so.1", "libwayland-client.so.0"}) {
        if (void* lib = dlopen(name, RTLD_NOW | RTLD_LOCAL)) dlclose(lib);
        else std::fprintf(stderr, "[flip-display] dlopen %s: %s\n", name, dlerror());
    }
    // One more attempt with SDL's video-category debug logging on: KMSDRM reports every DRM node it
    // rejected (no connector, no mode, DRM master held elsewhere) and Wayland the missing socket.
    SDL_SetLogPriority(SDL_LOG_CATEGORY_VIDEO, SDL_LOG_PRIORITY_DEBUG);
    return SDL_InitSubSystem(SDL_INIT_VIDEO);
}
void initSdl() {
    // SDL3's atomic KMSDRM path fails every flip on the RG351P and the Flip ("Failed to issue atomic
    // commit on pageflip"); the legacy drmModePageFlip path works on both. Set it unless the user did.
    setenv("SDL_KMSDRM_ATOMIC", "0", 0);
    if (!initSdlVideo()) failSdl("Cannot initialize SDL video");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1); // GLES 3.1: Dawn's GL interop needs glMemoryBarrier.
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    // Create the window at the panel's native mode. KMSDRM only has the panel's mode(s); requesting a
    // size with no matching mode (e.g. 640x480 on a 320x480 panel) leaves the window with no EGL
    // surface. Aurora still renders at renderWidth x renderHeight; the present worker scales to the panel.
    int winW = static_cast<int>(renderWidth), winH = static_cast<int>(renderHeight);
    int numDisplays = 0;
    if (SDL_DisplayID* displays = SDL_GetDisplays(&numDisplays); displays) {
        if (numDisplays > 0) {
            if (const SDL_DisplayMode* dm = SDL_GetCurrentDisplayMode(displays[0]); dm && dm->w > 0) {
                winW = dm->w; winH = dm->h;
                std::fprintf(stderr, "[flip-display] SDL current display mode %dx%d@%.0f\n", dm->w, dm->h, dm->refresh_rate);
            }
        }
        SDL_free(displays);
    }
    sdlWindow = SDL_CreateWindow("Melee Native", winW, winH,
                                 SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
    if (!sdlWindow) failSdl("Cannot create SDL window");
    sdlContext = SDL_GL_CreateContext(sdlWindow);
    if (!sdlContext) failSdl("Cannot create SDL GL context");
    if (!SDL_GL_MakeCurrent(sdlWindow, sdlContext)) failSdl("Cannot make the SDL GL context current");
    display = eglGetCurrentDisplay();
    if (display == EGL_NO_DISPLAY) display = static_cast<EGLDisplay>(SDL_EGL_GetCurrentDisplay());
    if (display == EGL_NO_DISPLAY) failSdl("SDL did not create an EGL display");
    // The context's bound draw surface is SDL's window surface; use it directly (SDL_EGL_GetWindowSurface
    // is not always populated on KMSDRM).
    sdlSurface = eglGetCurrentSurface(EGL_DRAW);
    if (sdlSurface == EGL_NO_SURFACE) sdlSurface = static_cast<EGLSurface>(SDL_EGL_GetWindowSurface(sdlWindow));
    if (sdlSurface == EGL_NO_SURFACE) failSdl("SDL did not create a window EGL surface");
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(sdlWindow, &pw, &ph);
    displayWidth = pw > 0 ? static_cast<uint32_t>(pw) : renderWidth;
    displayHeight = ph > 0 ? static_cast<uint32_t>(ph) : renderHeight;
    // Handhelds with a portrait-mounted panel (RG351P/M/V: 320x480) report a portrait mode and no
    // orientation property; the game is 4:3 landscape, so rotate unless MELEE_FLIP_ROTATE is set.
    if (!std::getenv("MELEE_FLIP_ROTATE") && displayHeight > displayWidth) displayRotation = 270;
    // Aurora's swapchain (Dawn) still needs an EGL native window to wrap. In threaded-present mode
    // Dawn never presents through it (the worker presents SDL's surface instead), so it is only ever
    // allocated, never shown.
    const char* videoDriver = SDL_GetCurrentVideoDriver();
    if (videoDriver && !std::strcmp(videoDriver, "wayland")) {
        // Wayland compositor (ROCKNIX's Sway/Weston). EGL allows one surface per wl_egl_window and
        // SDL's window already has one, so wrap a hidden window's wl_surface: it has no shell role,
        // so the compositor never maps it. libwayland-egl is loaded like SDL loads it, at run time.
        dawnHiddenWindow = SDL_CreateWindow("Melee Native (Dawn)", static_cast<int>(displayWidth),
                                            static_cast<int>(displayHeight), SDL_WINDOW_HIDDEN);
        void* surface = dawnHiddenWindow ? SDL_GetPointerProperty(SDL_GetWindowProperties(dawnHiddenWindow),
                                                                  SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr)
                                         : nullptr;
        if (!surface) failSdl("Cannot create the Dawn scratch Wayland surface");
        void* lib = dlopen("libwayland-egl.so.1", RTLD_NOW | RTLD_GLOBAL);
        const auto create = lib ? reinterpret_cast<void* (*)(void*, int, int)>(dlsym(lib, "wl_egl_window_create")) : nullptr;
        wlEglWindowDestroy = lib ? reinterpret_cast<void (*)(void*)>(dlsym(lib, "wl_egl_window_destroy")) : nullptr;
        if (!create || !wlEglWindowDestroy) failSdl("libwayland-egl.so.1 unavailable");
        dawnWlWindow = create(surface, static_cast<int>(displayWidth), static_cast<int>(displayHeight));
        if (!dawnWlWindow) failSdl("Cannot create the Dawn scratch Wayland EGL window");
    } else if (videoDriver && !std::strcmp(videoDriver, "sdl2")) {
        // SDL3-over-SDL2 shim (PortMaster): the CFW's own SDL2 owns the display, whichever stack it
        // uses (KMSDRM, fbdev, Wayland), and exposes no platform handles. Dawn's swapchain therefore
        // wraps no native window at all: MeleeFlipNativeWindow() stays null and the Dawn patch backs
        // the surface with a pbuffer. The worker presents into SDL's window surface as on the other paths.
        std::fprintf(stderr, "[flip-display] SDL2 shim: Dawn's swapchain uses a pbuffer\n");
    } else {
        // KMSDRM: borrow SDL's GBM device and hand Dawn a scratch GBM surface.
        sdlGbm = static_cast<gbm_device*>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(sdlWindow), SDL_PROP_WINDOW_KMSDRM_GBM_DEVICE_POINTER, nullptr));
        if (!sdlGbm) failSdl("MELEE_FLIP_DISPLAY=sdl needs the KMSDRM, Wayland or sdl2 video driver (no GBM device on the SDL window)");
        dawnWindow = gbm_surface_create(sdlGbm, displayWidth, displayHeight, GBM_FORMAT_ARGB8888,
                                        GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
        if (!dawnWindow) failSdl("Cannot create the Dawn scratch GBM surface");
    }
    // SDL3's KMSDRM swap adds DRM_MODE_PAGE_FLIP_ASYNC whenever SDL's swap interval is 0 and the
    // kernel advertises DRM_CAP_ASYNC_PAGE_FLIP; the RG351P's Rockchip 4.4 kernel advertises it but
    // rejects the flip ("Could not queue pageflip: -22"). SDL records the interval here, not from the
    // worker's raw eglSwapInterval. MELEE_FLIP_SWAP_INTERVAL overrides.
    {
        const char* interval = std::getenv("MELEE_FLIP_SWAP_INTERVAL");
        if (!SDL_GL_SetSwapInterval(interval ? std::atoi(interval) : 1))
            std::fprintf(stderr, "[flip-display] SDL_GL_SetSwapInterval failed: %s\n", SDL_GetError());
    }
    // Release the surface from this context so the present worker can bind it on its own thread.
    SDL_GL_MakeCurrent(sdlWindow, nullptr);
    std::atexit(cleanupSdl);
    std::fprintf(stderr, "[flip-display] SDL display (%s), panel %ux%u, render %ux%u, rotate %d\n",
                 videoDriver ? videoDriver : "?",
                 displayWidth, displayHeight, renderWidth, renderHeight, displayRotation);
    MeleeFlipInitPresenter();
}
}

bool MeleeFlipSdlDisplaySelected() { return backend == DisplayBackend::Sdl; }

void MeleeFlipInitDisplay() {
    if (backend == DisplayBackend::Sdl) { initSdl(); return; }
    // Select a connected panel and a CRTC for it rather than hardcoding object IDs.
    // MELEE_DRM_DEVICE picks the DRM node (the Flip's stock firmware scans out on card0),
    // MELEE_DRM_CONNECTOR the index into the device's connector list when the first
    // connected one is not the panel.
    const char* node = std::getenv("MELEE_DRM_DEVICE");
    fd = open(node ? node : "/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) fail("Cannot open DRM device");
    std::atexit(cleanup);
    auto* resources = drmModeGetResources(fd);
    if (!resources) fail("Cannot enumerate display resources");
    long wanted = -1;
    if (const char* index = std::getenv("MELEE_DRM_CONNECTOR"); index && *index) {
        char* end = nullptr;
        wanted = std::strtol(index, &end, 0);
        if (!end || *end || wanted < 0 || wanted >= resources->count_connectors) fail("MELEE_DRM_CONNECTOR is not a valid connector index");
    }
    bool havePreferred = false;
    for (int i = 0; i < resources->count_connectors && !connector; ++i) {
        if (wanted >= 0 && i != wanted) continue;
        auto* candidate = drmModeGetConnector(fd, resources->connectors[i]);
        if (!candidate) continue;
        if (candidate->connection == DRM_MODE_CONNECTED && candidate->count_modes) {
            // The active encoder's CRTC when the firmware already drives the panel,
            // otherwise the first CRTC one of the connector's encoders can use.
            uint32_t candidateCrtc = 0;
            if (auto* encoder = drmModeGetEncoder(fd, candidate->encoder_id)) {
                candidateCrtc = encoder->crtc_id;
                drmModeFreeEncoder(encoder);
            }
            for (int e = 0; e < candidate->count_encoders && !candidateCrtc; ++e) {
                auto* encoder = drmModeGetEncoder(fd, candidate->encoders[e]);
                if (!encoder) continue;
                for (int c = 0; c < resources->count_crtcs && !candidateCrtc; ++c)
                    if (encoder->possible_crtcs & (1u << c)) candidateCrtc = resources->crtcs[c];
                drmModeFreeEncoder(encoder);
            }
            if (candidateCrtc) {
                connector = candidate->connector_id;
                crtc = candidateCrtc;
                saved = drmModeGetCrtc(fd, crtc);
                // Preferred mode first, then whatever the firmware is scanning out, then the
                // connector's first mode.
                mode = candidate->modes[0];
                for (int m = 0; m < candidate->count_modes; ++m) {
                    if (candidate->modes[m].type & DRM_MODE_TYPE_PREFERRED) { mode = candidate->modes[m]; havePreferred = true; break; }
                }
                if (!havePreferred && saved && saved->mode_valid) mode = saved->mode;
            }
        }
        drmModeFreeConnector(candidate);
    }
    drmModeFreeResources(resources);
    if (!connector) fail("No active connected display");
    if (!mode.hdisplay || !mode.vdisplay) fail("Display mode has no size");
    displayWidth = mode.hdisplay;
    displayHeight = mode.vdisplay;
    device = gbm_create_device(fd);
    if (!device) fail("Cannot create GBM device");
    window = gbm_surface_create(device, displayWidth, displayHeight, GBM_FORMAT_ARGB8888,
                                GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!window) fail("Cannot create GBM surface");
    auto getDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (!getDisplay) fail("EGL platform display extension unavailable");
    display = getDisplay(EGL_PLATFORM_GBM_KHR, device, nullptr);
    EGLint major, minor;
    if (!eglInitialize(display, &major, &minor)) fail("Cannot initialize EGL display");
    std::fprintf(stderr, "[flip-display] EGL %d.%d, DRM connector %u, CRTC %u, %ux%u@%u (%s)\n", major, minor, connector, crtc,
                 displayWidth, displayHeight, mode.vrefresh, havePreferred ? "preferred mode" : "current mode");
    MeleeFlipInitPresenter();
}
extern "C" void MeleeFlipDisplaySize(unsigned* width, unsigned* height) { *width = renderWidth; *height = renderHeight; }
extern "C" void MeleeFlipPanelSize(unsigned* width, unsigned* height) { *width = displayWidth; *height = displayHeight; }
extern "C" int MeleeFlipRotation() { return displayRotation; }
// Dawn's swapchain wraps this native window: the DRM path's scanout GBM surface, or (SDL path) the
// scratch GBM surface or Wayland EGL window. The present worker presents SDL's own surface via
// MeleeFlipPresentSurface().
extern "C" void* MeleeFlipNativeWindow() {
    if (backend == DisplayBackend::Drm) return window;
    return dawnWlWindow ? dawnWlWindow : static_cast<void*>(dawnWindow); // null on the SDL2 shim: pbuffer
}
// Aurora reuses this window instead of creating its own (see aurora window.cpp create_window).
extern "C" SDL_Window* MeleeFlipSdlWindow() { return backend == DisplayBackend::Sdl ? sdlWindow : nullptr; }
extern "C" void* MeleeFlipEGLDisplay() { return display; }
// The present worker swaps this surface. SDL owns it (its window surface) on the SDL path.
extern "C" void* MeleeFlipPresentSurface() { return sdlSurface; }
extern "C" int MeleeFlipUsesSdlDisplay() { return backend == DisplayBackend::Sdl ? 1 : 0; }
// SDL_GLContext is the EGLContext on SDL's EGL backends, so the worker's shared context can be bound
// through SDL, which records the thread's current window for SDL_GL_SwapWindow.
extern "C" int MeleeFlipMakeCurrentSdl(void* context) {
    if (SDL_GL_MakeCurrent(sdlWindow, static_cast<SDL_GLContext>(context))) return 1;
    std::fprintf(stderr, "[flip-display] SDL_GL_MakeCurrent on the present worker failed: %s\n", SDL_GetError());
    return 0;
}
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
    if (backend == DisplayBackend::Sdl) {
        // SDL's KMSDRM SwapWindow does the eglSwapBuffers of our worker's rendered frame AND the
        // DRM page flip. The worker rendered into SDL's surface (its default framebuffer) already.
        if (!SDL_GL_SwapWindow(sdlWindow)) {
            static bool reported = false;
            if (!reported) { reported = true; std::fprintf(stderr, "[flip-display] SDL_GL_SwapWindow failed: %s\n", SDL_GetError()); }
        }
        return;
    }
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
    if (drmModeAddFB(fd, displayWidth, displayHeight, 32, 32, gbm_bo_get_stride(bo), gbm_bo_get_handle(bo).u32, &fb))
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
