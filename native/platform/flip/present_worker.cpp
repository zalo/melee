// Present completed surface textures on a shared GLES context. The rendering
// context remains surfaceless and can submit the following frame while this
// thread waits for rendering, EGL, or panel scanout.
#include <dawn/native/OpenGLBackend.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>

extern "C" void MeleeFlipPresent();
extern "C" int MeleeFlipRotation();

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
void recycle_texture(const Frame& frame);
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


// Rotating, aspect-preserving present. glBlitFramebuffer cannot rotate, so when the panel needs
// a rotation (portrait scanout, e.g. Anbernic RG351P) we draw the scene texture as a textured
// quad, letterboxed to preserve the 4:3 frame. rotation 0 keeps the fast blit path.
int g_rotation = 0;
GLuint g_blitProg = 0, g_blitVbo = 0, g_blitVao = 0;
GLint g_locHalf = -1, g_locUV = -1, g_locTex = -1;
GLuint compile_shader(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; glGetShaderInfoLog(sh, sizeof log, nullptr, log); std::fprintf(stderr, "[flip-present] shader: %s\n", log); }
    return sh;
}
void init_rotated_blit() {
    const char* vs =
        "#version 300 es\n"
        "layout(location=0) in vec2 aPos;\n"
        "uniform vec2 uHalf; uniform mat2 uUV;\n"
        "out vec2 vTex;\n"
        "void main(){\n"
        "  gl_Position = vec4(aPos * uHalf, 0.0, 1.0);\n"
        "  vec2 uv = aPos * 0.5 + 0.5; uv.y = 1.0 - uv.y;\n"
        "  vTex = uUV * (uv - 0.5) + 0.5;\n"
        "}\n";
    const char* fs =
        "#version 300 es\n"
        "precision mediump float;\n"
        "in vec2 vTex; uniform sampler2D uTex; out vec4 o;\n"
        "void main(){ o = texture(uTex, vTex); }\n";
    g_blitProg = glCreateProgram();
    GLuint v = compile_shader(GL_VERTEX_SHADER, vs), f = compile_shader(GL_FRAGMENT_SHADER, fs);
    glAttachShader(g_blitProg, v); glAttachShader(g_blitProg, f);
    glBindAttribLocation(g_blitProg, 0, "aPos");
    glLinkProgram(g_blitProg);
    glDeleteShader(v); glDeleteShader(f);
    g_locHalf = glGetUniformLocation(g_blitProg, "uHalf");
    g_locUV = glGetUniformLocation(g_blitProg, "uUV");
    g_locTex = glGetUniformLocation(g_blitProg, "uTex");
    const float quad[] = {-1,-1, 1,-1, -1,1, 1,1};
    glGenVertexArrays(1, &g_blitVao); glBindVertexArray(g_blitVao);
    glGenBuffers(1, &g_blitVbo); glBindBuffer(GL_ARRAY_BUFFER, g_blitVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);
}
void draw_rotated(GLuint tex, uint32_t fw, uint32_t fh, int sw, int sh, int rot) {
    if (!g_blitProg) init_rotated_blit();
    const double pi = 3.14159265358979323846;
    const double a = -rot * pi / 180.0;           // rotate the sampled image by +rot
    const float c = (float)std::cos(a), s = (float)std::sin(a);
    const bool swap = (rot % 180) != 0;
    const double da = swap ? (double)fh / fw : (double)fw / fh; // displayed aspect (w/h)
    const double sa = (double)sw / sh;
    float halfW = 1.f, halfH = 1.f;
    if (da >= sa) halfH = (float)(sa / da); else halfW = (float)(da / sa);
    glViewport(0, 0, sw, sh);
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST);
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(g_blitProg);
    glBindVertexArray(g_blitVao);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glUniform1i(g_locTex, 0);
    glUniform2f(g_locHalf, halfW, halfH);
    const float uv[4] = { c, s, -s, c };          // mat2 column-major
    glUniformMatrix2fv(g_locUV, 1, GL_FALSE, uv);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
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
    g_rotation = ((MeleeFlipRotation() % 360) + 360) % 360;
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
        if (g_rotation != 0) {
            draw_rotated(frame.texture, frame.width, frame.height, width, height, g_rotation);
        } else {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, frame.texture, 0);
            if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                fail("Incomplete presentation framebuffer");
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            glBlitFramebuffer(0, 0, frame.width, frame.height, 0, height, width, 0,
                              GL_COLOR_BUFFER_BIT, GL_LINEAR);
        }
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
        recycle_texture(frame);
        const auto now = Clock::now();
        if (profiling) std::fprintf(stderr, "[flip-thread-present] frame_ms=%.3f present_ms=%.3f\n",
            std::chrono::duration<double, std::milli>(now-previous).count(),
            std::chrono::duration<double, std::milli>(now-start).count());
        previous = now;
    }
    glDeleteFramebuffers(1, &readFbo);
    eglMakeCurrent(workerDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

// Presented textures return to a small pool the swapchain draws from, so the
// producer neither allocates storage nor validates a new framebuffer each frame.
struct Recycled { GLuint texture; uint32_t width, height; };
std::mutex poolMutex;
std::vector<Recycled> pool;
const bool poolEnabled = [] { const char* v = std::getenv("MELEE_FLIP_SWAPCHAIN_POOL"); return !v || std::strcmp(v, "0"); }();
void recycle_texture(const Frame& frame) {
    if (!poolEnabled) { glDeleteTextures(1, &frame.texture); return; }
    std::lock_guard lock(poolMutex);
    if (pool.size() >= 4) { glDeleteTextures(1, &frame.texture); return; }
    pool.push_back({frame.texture, frame.width, frame.height});
}
GLuint acquire(uint32_t width, uint32_t height) {
    std::lock_guard lock(poolMutex);
    for (auto it = pool.begin(); it != pool.end(); ++it) {
        if (it->width == width && it->height == height) { const GLuint t = it->texture; pool.erase(it); return t; }
    }
    return 0;
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
            dawn::native::opengl::SetGLInteropPresentCallbacks(nullptr);
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
    static const dawn::native::opengl::GLInteropPresentCallbacks callbacks{
        submit, MeleeFlipShutdownPresenter, acquire};
    if (enabled) dawn::native::opengl::SetGLInteropPresentCallbacks(&callbacks);
}
