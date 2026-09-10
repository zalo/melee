// Standalone GLES 3.1 draw-call cost micro-benchmark for the Miyoo Flip.
// Uses a surfaceless context on the GBM device, so it needs no display
// ownership and can run while MainUI is up. Reports CPU microseconds per
// iteration for a draw combined with one kind of state change each.
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}
static void die(const char* what) {
    fprintf(stderr, "bench: %s (egl 0x%x gl 0x%x)\n", what, eglGetError(), glGetError());
    exit(1);
}
static GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "%s\n", log);
        die("shader compile");
    }
    return s;
}
static GLuint program(const char* vs, const char* fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, compile(GL_VERTEX_SHADER, vs));
    glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) die("program link");
    return p;
}

static const char* kVS =
    "#version 310 es\n"
    "layout(location=0) in vec3 pos; layout(location=1) in uvec4 rec; layout(location=2) in vec2 uv;\n"
    "struct Record { vec4 v[256]; };\n"
    "layout(std140, binding=0) uniform Block { Record recs[16]; };\n"
    "uniform uint imm[16];\n"
    "out vec2 vuv; flat out uint vrec;\n"
    "void main(){ vrec = rec.x & 15u; vec4 m = recs[vrec].v[0] + recs[vrec].v[1] * float(imm[0]);\n"
    "  gl_Position = vec4(pos, 1.0) + m * 0.0; vuv = uv; }\n";
static const char* kFS =
    "#version 310 es\n"
    "precision highp float;\n"
    "struct Record { vec4 v[256]; };\n"
    "layout(std140, binding=0) uniform Block { Record recs[16]; };\n"
    "uniform sampler2D tex0; uniform sampler2D tex1;\n"
    "in vec2 vuv; flat in uint vrec; layout(location=0) out vec4 color;\n"
    "void main(){ color = texture(tex0, vuv) * recs[vrec].v[2] + texture(tex1, vuv) * recs[vrec].v[3]; }\n";

typedef struct { GLuint prog[2], tex[4], samp[2], ubo, vbo, ebo, inst, vao, fbo, rt; GLint imm[2]; } Ctx;

static void draw(int range) {
    if (range) glDrawRangeElements(GL_TRIANGLES, 0, 3, 6, GL_UNSIGNED_SHORT, 0);
    else glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
}

#define BENCH(name, body)                                                                          \
    do {                                                                                           \
        glFinish();                                                                                \
        for (int i = 0; i < 64; ++i) { body; }                                                     \
        glFinish();                                                                                \
        double t0 = now_us();                                                                      \
        for (int i = 0; i < N; ++i) { body; }                                                      \
        double t1 = now_us();                                                                      \
        glFlush();                                                                                 \
        double t2 = now_us();                                                                      \
        glFinish();                                                                                \
        double t3 = now_us();                                                                      \
        printf("%-44s cpu %7.2f us/iter  +flush %7.2f us/iter  gpu-wait %7.2f us/iter\n", name,     \
               (t1 - t0) / N, (t2 - t0) / N, (t3 - t0) / N);                                       \
    } while (0)

int main(int argc, char** argv) {
    const int N = argc > 1 ? atoi(argv[1]) : 2000;
    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) die("open card0");
    struct gbm_device* gbm = gbm_create_device(fd);
    if (!gbm) die("gbm_create_device");
    PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplay =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = getPlatformDisplay ? getPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL)
                                        : eglGetDisplay((EGLNativeDisplayType)gbm);
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL)) die("eglInitialize");
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint cfgAttribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                 EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    EGLConfig cfg;
    EGLint n = 0;
    if (!eglChooseConfig(dpy, cfgAttribs, &cfg, 1, &n) || n < 1) {
        const EGLint anyAttribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE};
        if (!eglChooseConfig(dpy, anyAttribs, &cfg, 1, &n) || n < 1) die("eglChooseConfig");
    }
    const EGLint ctxAttribs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttribs);
    if (ctx == EGL_NO_CONTEXT) die("eglCreateContext");
    EGLSurface surf = EGL_NO_SURFACE;
    if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
        const EGLint pb[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
        surf = eglCreatePbufferSurface(dpy, cfg, pb);
        if (surf == EGL_NO_SURFACE || !eglMakeCurrent(dpy, surf, surf, ctx)) die("eglMakeCurrent");
    }
    printf("GL_RENDERER: %s\nGL_VERSION: %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    GLint maxBlock = 0, uboAlign = 0;
    glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &maxBlock);
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &uboAlign);
    printf("max uniform block %d, ubo offset alignment %d\n", maxBlock, uboAlign);

    Ctx c;
    c.prog[0] = program(kVS, kFS);
    c.prog[1] = program(kVS, kFS);
    for (int p = 0; p < 2; ++p) {
        glUseProgram(c.prog[p]);
        glUniform1i(glGetUniformLocation(c.prog[p], "tex0"), 0);
        glUniform1i(glGetUniformLocation(c.prog[p], "tex1"), 1);
        c.imm[p] = glGetUniformLocation(c.prog[p], "imm");
    }
    glGenTextures(4, c.tex);
    static unsigned char pixels[64 * 64 * 4];
    for (int i = 0; i < 4; ++i) {
        glBindTexture(GL_TEXTURE_2D, c.tex[i]);
        memset(pixels, 40 * (i + 1), sizeof pixels);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 64, 64);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    glGenSamplers(2, c.samp);
    glSamplerParameteri(c.samp[0], GL_TEXTURE_WRAP_S, GL_REPEAT);
    glSamplerParameteri(c.samp[1], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glGenBuffers(1, &c.ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, c.ubo);
    glBufferData(GL_UNIFORM_BUFFER, 1 << 20, NULL, GL_DYNAMIC_DRAW);
    static float zeros[65536 / 4];
    for (int w = 0; w < 16; ++w) glBufferSubData(GL_UNIFORM_BUFFER, w * 65536, 65536, zeros);
    glBindBufferRange(GL_UNIFORM_BUFFER, 0, c.ubo, 0, 65536);
    const float verts[] = {-0.5f, -0.5f, 0, 0, 0, 0.5f, -0.5f, 0, 1, 0, 0.5f, 0.5f, 0, 1, 1, -0.5f, 0.5f, 0, 0, 1};
    const unsigned short idx[] = {0, 1, 2, 2, 3, 0};
    static unsigned int inst[64 * 4];
    glGenVertexArrays(1, &c.vao);
    glBindVertexArray(c.vao);
    glGenBuffers(1, &c.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, c.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof verts, verts, GL_STATIC_DRAW);
    glGenBuffers(1, &c.ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, c.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof idx, idx, GL_STATIC_DRAW);
    glGenBuffers(1, &c.inst);
    glBindBuffer(GL_ARRAY_BUFFER, c.inst);
    glBufferData(GL_ARRAY_BUFFER, sizeof inst, inst, GL_STATIC_DRAW);
    glBindVertexBuffer(0, c.vbo, 0, 20);
    glVertexAttribFormat(0, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexAttribBinding(0, 0);
    glEnableVertexAttribArray(0);
    glVertexAttribFormat(2, 2, GL_FLOAT, GL_FALSE, 12);
    glVertexAttribBinding(2, 0);
    glEnableVertexAttribArray(2);
    glVertexAttribI4ui(1, 0, 0, 0, 0); // location 1 disabled: constant attribute
    glGenTextures(1, &c.rt);
    glBindTexture(GL_TEXTURE_2D, c.rt);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 64, 64);
    glGenFramebuffers(1, &c.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, c.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c.rt, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) die("fbo");
    glViewport(0, 0, 64, 64);
    glUseProgram(c.prog[0]);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, c.tex[0]);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, c.tex[1]);
    glActiveTexture(GL_TEXTURE0);
    if (glGetError() != GL_NO_ERROR) die("setup");
    unsigned int immData[16] = {0};

    BENCH("warm-up draw only", draw(1));
    BENCH("draw only (glDrawRangeElements)", draw(1));
    BENCH("draw only (glDrawElements)", draw(0));
    BENCH("glBindBufferRange(UBO 64K window) + draw",
          { glBindBufferRange(GL_UNIFORM_BUFFER, 0, c.ubo, (i & 7) * 65536, 65536); draw(1); });
    BENCH("glBindBufferRange(UBO 64K, offset+=4K) + draw",
          { glBindBufferRange(GL_UNIFORM_BUFFER, 0, c.ubo, (i & 63) * 4096, 65536); draw(1); });
    BENCH("glBindTexture(alternate, unit 0) + draw",
          { glBindTexture(GL_TEXTURE_2D, c.tex[i & 1]); draw(1); });
    BENCH("2x glBindTexture(units 0,1) + draw",
          { glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, c.tex[i & 1]);
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, c.tex[2 + (i & 1)]);
            glActiveTexture(GL_TEXTURE0); draw(1); });
    BENCH("glBindSampler(alternate) + draw", { glBindSampler(0, c.samp[i & 1]); draw(1); });
    glBindSampler(0, 0);
    BENCH("glTexParameteri(wrap) on bound texture + draw",
          { glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (i & 1) ? GL_REPEAT : GL_CLAMP_TO_EDGE); draw(1); });
    BENCH("glUseProgram(alternate) + draw", { glUseProgram(c.prog[i & 1]); draw(1); });
    glUseProgram(c.prog[0]);
    BENCH("glVertexAttribI4ui(constant attr) + draw", { glVertexAttribI4ui(1, i, 0, 0, 0); draw(1); });
    glVertexAttribI4ui(1, 0, 0, 0, 0);
    glVertexAttribIFormat(1, 4, GL_UNSIGNED_INT, 0);
    glVertexAttribBinding(1, 1);
    glVertexBindingDivisor(1, 1);
    glEnableVertexAttribArray(1);
    BENCH("glBindVertexBuffer(instanced attr offset) + draw",
          { glBindVertexBuffer(1, c.inst, (i & 63) * 16, 16); draw(1); });
    glDisableVertexAttribArray(1);
    BENCH("glBindVertexBuffer(binding 0, offset) + draw",
          { glBindVertexBuffer(0, c.vbo, 0, 20); draw(1); });
    BENCH("glUniform1uiv(16 uints) + draw", { immData[0] = i; glUniform1uiv(c.imm[0], 16, immData); draw(1); });
    BENCH("glEnable/Disable(GL_BLEND) toggle + draw",
          { if (i & 1) glEnable(GL_BLEND); else glDisable(GL_BLEND); draw(1); });
    glDisable(GL_BLEND);
    BENCH("glDepthFunc toggle + draw", { glDepthFunc((i & 1) ? GL_LESS : GL_LEQUAL); draw(1); });
    BENCH("typical: texture + UBO range + program + draw",
          { glUseProgram(c.prog[i & 1]); glBindTexture(GL_TEXTURE_2D, c.tex[i & 1]);
            glBindBufferRange(GL_UNIFORM_BUFFER, 0, c.ubo, (i & 63) * 4096, 65536); draw(1); });
    BENCH("texture + UBO range + draw (same program)",
          { glBindTexture(GL_TEXTURE_2D, c.tex[i & 1]);
            glBindBufferRange(GL_UNIFORM_BUFFER, 0, c.ubo, (i & 63) * 4096, 65536); draw(1); });
    BENCH("texture + constant attr + draw (same program)",
          { glBindTexture(GL_TEXTURE_2D, c.tex[i & 1]); glVertexAttribI4ui(1, i, 0, 0, 0); draw(1); });
    if (glGetError() != GL_NO_ERROR) die("bench");
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    if (surf != EGL_NO_SURFACE) eglDestroySurface(dpy, surf);
    eglTerminate(dpy);
    gbm_device_destroy(gbm);
    close(fd);
    return 0;
}
