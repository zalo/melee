// Miyoo Flip presentation: a GBM surface on the panel's DRM CRTC, handed to Aurora as an
// EGL native window (Dawn's EGL swapchain renders and swaps into it) and scanned out with a
// DRM page flip after every eglSwapBuffers. This is the pre-v67 presentation path; there is
// no direct-GL escape hatch, presentation thread, or per-draw barrier interception here.
#include "display.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

extern "C" void MeleeFlipPresent();

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
// MELEE_FLIP_ASYNC_PRESENT=1 defers the page-flip wait to the next present, so the render
// worker queues the following frame instead of blocking on the vblank. Default: synchronous.
const bool asyncPresent = [] { const auto* s = std::getenv("MELEE_FLIP_ASYNC_PRESENT"); return s && !std::strcmp(s, "1"); }();
const bool profiling = std::getenv("MELEE_FLIP_PROFILE") != nullptr;
PFNEGLSWAPBUFFERSPROC realSwapBuffers;
using ProfileClock = std::chrono::steady_clock;
auto lastPresent = ProfileClock::now();
double elapsed(ProfileClock::time_point start) {
    return std::chrono::duration<double, std::milli>(ProfileClock::now() - start).count();
}
void wait_pending_flip();

[[noreturn]] void fail(const char* message) {
    std::fprintf(stderr, "[flip-display] %s (errno=%d, EGL=0x%x)\n", message, errno, eglGetError());
    std::exit(1);
}
void cleanup() {
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
    drmEventContext events{}; events.version = 2; events.page_flip_handler = flipped;
    while (pendingFlip) {
        pollfd pfd{fd, POLLIN, 0};
        const int result = poll(&pfd, 1, 2000);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0 || !(pfd.revents & POLLIN) || drmHandleEvent(fd, &events)) fail("Page flip wait failed");
    }
}
// Dawn's EGL swapchain presents with eglSwapBuffers on the GBM surface; the finished buffer
// is scanned out right after, on the same (render worker) thread.
EGLBoolean swapBuffersAndScanOut(EGLDisplay dpy, EGLSurface surface) {
    const EGLBoolean swapped = realSwapBuffers(dpy, surface);
    if (swapped) MeleeFlipPresent();
    return swapped;
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
    std::fprintf(stderr, "[flip-display] EGL %d.%d, DRM connector %u, CRTC %u, 640x480, %s page-flip wait\n",
                 major, minor, connector, crtc, asyncPresent ? "deferred" : "synchronous");
}
extern "C" void* MeleeFlipNativeWindow() { return window; }
extern "C" void* MeleeFlipEGLDisplay() { return display; }
extern "C" __eglMustCastToProperFunctionPointerType MeleeFlipEGLProc(const char* name) {
    const auto proc = eglGetProcAddress(name);
    if (!std::strcmp(name, "eglSwapBuffers")) {
        realSwapBuffers = proc ? reinterpret_cast<PFNEGLSWAPBUFFERSPROC>(proc) : eglSwapBuffers;
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(swapBuffersAndScanOut);
    }
    return proc;
}
extern "C" void MeleeFlipPresent() {
    const auto presentStart = profiling ? ProfileClock::now() : ProfileClock::time_point{};
    if (asyncPresent) {
        wait_pending_flip();
        if (retiredFB) drmModeRmFB(fd, retiredFB);
        if (retiredBO) gbm_surface_release_buffer(window, retiredBO);
        retiredFB = 0; retiredBO = nullptr;
    }
    auto* bo = gbm_surface_lock_front_buffer(window);
    if (!bo) fail("Cannot lock rendered frame");
    const double lockMs = profiling ? elapsed(presentStart) : 0;
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
        retiredFB = previousFB; retiredBO = previousBO;
    } else {
        if (previousFB) drmModeRmFB(fd, previousFB);
        if (previousBO) gbm_surface_release_buffer(window, previousBO);
    }
    previousBO = bo;
    previousFB = fb;
    if (profiling) {
        // frame_ms: interval between presents; lock_ms: gbm front-buffer lock (waits for the
        // GPU when the swap has not finished); drm_ms: AddFB + page flip (+ vblank wait unless
        // MELEE_FLIP_ASYNC_PRESENT=1).
        std::fprintf(stderr, "[flip-present] frame_ms=%.3f lock_ms=%.3f drm_ms=%.3f\n",
                     elapsed(lastPresent), lockMs, elapsed(presentStart) - lockMs);
        lastPresent = ProfileClock::now();
    }
}
