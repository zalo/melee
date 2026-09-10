// Present completed surface textures on a shared GLES context. The rendering
// context remains surfaceless and can submit the following frame while this
// thread waits for rendering, EGL, or panel scanout.
#include <dawn/native/OpenGLBackend.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>

extern "C" void MeleeFlipPresent();

namespace {
using Clock = std::chrono::steady_clock;
const bool enabled = [] {
    const char* value = std::getenv("MELEE_FLIP_PRESENT_THREAD");
    return value && !std::strcmp(value, "1");
}();
const bool profiling = std::getenv("MELEE_FLIP_PROFILE") != nullptr;
std::atomic_bool disabled = false;
thread_local bool inWorker = false;
struct Frame { GLuint texture; GLsync ready; uint32_t width, height; };
std::mutex mutex;
std::condition_variable wake;
std::deque<Frame> frames;
std::thread worker;
bool stopping = false;
EGLDisplay workerDisplay = EGL_NO_DISPLAY;
EGLContext workerContext = EGL_NO_CONTEXT;
EGLSurface workerSurface = EGL_NO_SURFACE;

[[noreturn]] void fail(const char* message) {
    std::fprintf(stderr, "[flip-present-error] %s EGL=0x%x GL=0x%x\n",
                 message, eglGetError(), glGetError());
    // Do not run main/render-thread destructors on a failed GL worker. The
    // launcher restores governors and MainUI after the process exits.
    std::_Exit(1);
}

void run() {
    inWorker = true;
    eglBindAPI(EGL_OPENGL_ES_API);
    if (!eglMakeCurrent(workerDisplay, workerSurface, workerSurface, workerContext))
        fail("Cannot make presentation context current");
    eglSwapInterval(workerDisplay, 0); // DRM page flips pace the actual panel.
    EGLint width = 0, height = 0;
    if (!eglQuerySurface(workerDisplay, workerSurface, EGL_WIDTH, &width) ||
        !eglQuerySurface(workerDisplay, workerSurface, EGL_HEIGHT, &height))
        fail("Cannot query presentation surface");
    GLuint readFbo = 0;
    glGenFramebuffers(1, &readFbo);
    glDisable(GL_SCISSOR_TEST);
    auto previous = Clock::now();
    while (true) {
        Frame frame;
        {
            std::unique_lock lock(mutex);
            wake.wait(lock, [] { return stopping || !frames.empty(); });
            if (frames.empty()) break;
            frame = frames.front(); frames.pop_front();
        }
        wake.notify_all();
        const auto start = Clock::now();
        // Server-side wait: every shared-texture read follows the producer's
        // fence. No CPU polling or global glFinish in the rendering thread.
        glWaitSync(frame.ready, 0, GL_TIMEOUT_IGNORED);
        glDeleteSync(frame.ready);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, frame.texture, 0);
        if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            fail("Incomplete presentation framebuffer");
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, frame.width, frame.height, 0, height, width, 0,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);
        GLsync consumed = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        if (!consumed || glGetError() != GL_NO_ERROR) fail("Presentation blit failed");
        if (!eglSwapBuffers(workerDisplay, workerSurface)) fail("Presentation swap failed");
        MeleeFlipPresent();
        // Explicitly finish this texture's consumer before releasing ownership.
        // This wait is isolated on the presentation thread.
        GLenum result;
        do {
            result = glClientWaitSync(consumed, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000);
        } while (result == GL_TIMEOUT_EXPIRED);
        if (result == GL_WAIT_FAILED) fail("Presentation completion fence failed");
        glDeleteSync(consumed);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        glDeleteTextures(1, &frame.texture);
        const auto now = Clock::now();
        if (profiling) std::fprintf(stderr, "[flip-thread-present] frame_ms=%.3f present_ms=%.3f\n",
            std::chrono::duration<double, std::milli>(now-previous).count(),
            std::chrono::duration<double, std::milli>(now-start).count());
        previous = now;
    }
    glDeleteFramebuffers(1, &readFbo);
    eglMakeCurrent(workerDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

bool submit(GLuint texture, uint32_t width, uint32_t height, void* surface) {
    if (!worker.joinable()) {
        workerDisplay = eglGetCurrentDisplay();
        const EGLContext share = eglGetCurrentContext();
        workerSurface = static_cast<EGLSurface>(surface);
        EGLint configId = 0, count = 0;
        EGLConfig config = nullptr;
        if (!eglQuerySurface(workerDisplay, workerSurface, EGL_CONFIG_ID, &configId))
            fail("Cannot query presentation configuration");
        const EGLint choose[] = {EGL_CONFIG_ID, configId, EGL_NONE};
        if (!eglChooseConfig(workerDisplay, choose, &config, 1, &count) || count != 1)
            fail("Cannot choose presentation configuration");
        EGLint shareConfig = -1;
        if (eglQueryContext(workerDisplay, share, EGL_CONFIG_ID, &shareConfig) && shareConfig == 0)
            config = nullptr; // Match the producer's EGL_KHR_no_config_context.
        // Mirror the producer's version and robustness settings when creating
        // its share group, including the no-config-context case above.
        EGLint major = 3, minor = 1;
        eglQueryContext(workerDisplay, share, EGL_CONTEXT_MAJOR_VERSION_KHR, &major);
        eglQueryContext(workerDisplay, share, EGL_CONTEXT_MINOR_VERSION_KHR, &minor);
        GLboolean robust = GL_FALSE;
        GLint reset = 0x8261; // GL_NO_RESET_NOTIFICATION
        glGetBooleanv(0x90F3, &robust); // GL_CONTEXT_ROBUST_ACCESS
        glGetIntegerv(0x8256, &reset);  // GL_RESET_NOTIFICATION_STRATEGY
        glGetError();
        eglGetError();
        int eglMajor = 1, eglMinor = 4;
        std::sscanf(eglQueryString(workerDisplay,EGL_VERSION), "%d.%d", &eglMajor, &eglMinor);
        const bool egl15 = eglMajor > 1 || (eglMajor == 1 && eglMinor >= 5);
        const EGLint attributes[] = {EGL_CONTEXT_MAJOR_VERSION_KHR, major,
            EGL_CONTEXT_MINOR_VERSION_KHR, minor,
            egl15 ? EGL_CONTEXT_OPENGL_ROBUST_ACCESS : EGL_CONTEXT_OPENGL_ROBUST_ACCESS_EXT,
            robust ? EGL_TRUE : EGL_FALSE,
            egl15 ? EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY : EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT,
            reset == 0x8252 ? EGL_LOSE_CONTEXT_ON_RESET : EGL_NO_RESET_NOTIFICATION, EGL_NONE};
        workerContext = eglCreateContext(workerDisplay, config, share, attributes);
        if (workerContext == EGL_NO_CONTEXT) {
            std::fprintf(stderr,"[flip-present] shared context unavailable (ES %d.%d robust=%d EGL=0x%x); using synchronous presentation\n",
                         major,minor,robust,eglGetError());
            disabled.store(true);
            dawn::native::opengl::SetDirectGLPresentCallbacks(nullptr);
            return false;
        }
        stopping = false;
        worker = std::thread(run);
        std::fprintf(stderr, "[flip-present] shared GLES context, bounded queue of two frames\n");
    }
    GLsync ready = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!ready) fail("Cannot fence rendered surface texture");
    glFlush();
    std::unique_lock lock(mutex);
    wake.wait(lock, [] { return frames.size() < 2; });
    frames.push_back({texture, ready, width, height});
    lock.unlock();
    wake.notify_all();
    return true;
}
} // namespace

bool MeleeFlipThreadedPresentEnabled() { return enabled && !disabled.load(); }
bool MeleeFlipInPresentWorker() { return inWorker; }

void MeleeFlipShutdownPresenter() {
    if (!worker.joinable()) return;
    if (inWorker) { worker.detach(); return; }
    {
        std::lock_guard lock(mutex);
        stopping = true;
    }
    wake.notify_all();
    worker.join();
    eglDestroyContext(workerDisplay, workerContext);
    workerContext = EGL_NO_CONTEXT;
    workerSurface = EGL_NO_SURFACE;
    workerDisplay = EGL_NO_DISPLAY;
}

void MeleeFlipInitPresenter() {
    static const dawn::native::opengl::DirectGLPresentCallbacks callbacks{
        submit, MeleeFlipShutdownPresenter};
    if (enabled) dawn::native::opengl::SetDirectGLPresentCallbacks(&callbacks);
}
