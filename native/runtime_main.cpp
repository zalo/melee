#include <aurora/aurora.h>
#include <aurora/dvd.h>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <execinfo.h>
#include "os_runtime.h"
#include "platform_launcher.h"
#include <dolphin/dvd.h>
#include <cstring>
#include <filesystem>
#include <csignal>
#include <sys/stat.h>
#include <SDL3/SDL.h>
#include "disc_fonts.h"
#include "include/melee_netplay.h"
#ifdef MELEE_MIYOO_FLIP
#include "platform/flip/display.h"
#endif

static std::string executable_path, disc_path;
static bool graphical_launch;
extern "C" void MeleeNativeNetplayClearEnvironment(void);
#ifdef __linux__
static void clear_unclean_marker();
#endif
static void reset_game(int type) {
    // A reset the game asked for leaves an online session; the Online Play screen's own relaunch
    // keeps the MELEE_ONLINE_* variables it just set.
    MeleeNativeNetplayClearEnvironment();
#ifdef __linux__
    clear_unclean_marker(); // a deliberate restart is a clean exit
#endif
    if (type == 2) std::exit(0);
    char* arguments[] = {executable_path.data(), graphical_launch ? nullptr : disc_path.data(), nullptr};
    execv(arguments[0], arguments);
    std::perror("Native restart failed"); std::abort();
}

extern "C" int melee_game_main(void);
extern "C" void MeleeNativeSettingsLoad(const char* user_path);

#ifdef __linux__
// A crash lands in log.txt with enough to symbolize it against the release's unstripped binary: the
// signal, the faulting address, the executable's load base and the raw return addresses. (A tester's
// Pi 5 report was a bare "Segmentation fault" at renderer start-up, which this would have located.)
static void crash_handler(int signal, siginfo_t* info, void*) {
    static char text[256];
    int n = std::snprintf(text, sizeof text, "[crash] signal %d (%s) at address %p\n", signal, strsignal(signal),
                          info ? info->si_addr : nullptr);
    if (n > 0) (void)!write(2, text, static_cast<size_t>(n));
    if (FILE* maps = std::fopen("/proc/self/maps", "r")) {
        char line[512];
        while (std::fgets(line, sizeof line, maps)) {
            if (std::strstr(line, "melee") && (std::strstr(line, " r--p ") || std::strstr(line, " r-xp "))) {
                (void)!write(2, "[crash] exe map: ", 17);
                (void)!write(2, line, std::strlen(line));
                break;
            }
        }
        std::fclose(maps);
    }
    void* frames[48];
    const int count = backtrace(frames, 48);
    (void)!write(2, "[crash] stack:\n", 15);
    backtrace_symbols_fd(frames, count, 2);
    // Default action, so the launcher sees the signal exit.
    struct sigaction dfl{};
    dfl.sa_handler = SIG_DFL;
    sigaction(signal, &dfl, nullptr);
    raise(signal);
}
// Being stopped from outside (PortMaster's exit hotkey, the test harness) is not a crash: the cache
// marker goes away before the default action ends the process.
static void clear_unclean_marker();
static void terminate_handler(int signal) {
    clear_unclean_marker();
    struct sigaction dfl{};
    dfl.sa_handler = SIG_DFL;
    sigaction(signal, &dfl, nullptr);
    raise(signal);
}
static void installCrashHandler() {
    struct sigaction action{};
    action.sa_sigaction = crash_handler;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    for (int signal : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) sigaction(signal, &action, nullptr);
    struct sigaction stop{};
    stop.sa_handler = terminate_handler;
    for (int signal : {SIGTERM, SIGINT, SIGHUP}) sigaction(signal, &stop, nullptr);
}

// Cached pipeline configs only decode to the shaders of the build that wrote them, so every binary
// gets its own subdirectory (named after its size and mtime) and older ones are removed.
// Crash loops: a marker lives in the cache root while the game runs and goes away on a clean exit.
// It says "init" until the first logic frame and "running" afterwards. When a launch finds it still
// saying "init", the previous run died during start-up (a driver crash compiling a cached pipeline
// restarts the crash on every launch, as on a tester's Pi 5), so that cache is set aside and the
// game starts with an empty one. A run that got as far as playing keeps its cache whatever ended it,
// including the test harness's kill -9.
static std::string unclean_marker;
static void clear_unclean_marker() { if (!unclean_marker.empty()) std::remove(unclean_marker.c_str()); }
static void write_marker(const char* phase) {
    if (unclean_marker.empty()) return;
    if (FILE* marker = std::fopen(unclean_marker.c_str(), "w")) { std::fputs(phase, marker); std::fclose(marker); }
}
extern "C" void MeleeNativeRunPhaseRunning(void) {
    static bool done = false;
    if (!done) { done = true; write_marker("running"); }
}
static std::string pipelineCachePath(const std::string& root) {
    struct stat exe{};
    if (stat("/proc/self/exe", &exe) != 0) return root;
    char name[48];
    std::snprintf(name, sizeof(name), "pipeline-%llx-%llx", static_cast<unsigned long long>(exe.st_size),
                  static_cast<unsigned long long>(exe.st_mtime));
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(root, ec))
        if (entry.path().filename().string().rfind("pipeline-", 0) == 0 && entry.path().filename() != name)
            std::filesystem::remove_all(entry.path(), ec);
    std::filesystem::create_directories(root, ec);
    unclean_marker = root + "unclean-exit";
    if (FILE* marker = std::fopen(unclean_marker.c_str(), "r")) {
        char phase[16] = {};
        (void)!std::fgets(phase, sizeof phase, marker);
        std::fclose(marker);
        if (std::strncmp(phase, "init", 4) == 0) {
            std::fprintf(stderr, "[cache] the previous run died during start-up; starting with an empty pipeline cache (%s%s set aside)\n",
                         root.c_str(), name);
            std::filesystem::remove_all(root + name + ".crashed", ec);
            std::filesystem::rename(root + name, root + name + ".crashed", ec);
        } else {
            std::fprintf(stderr, "[cache] the previous run did not exit cleanly (phase %s); pipeline cache kept\n", phase[0] ? phase : "?");
        }
    }
    write_marker("init");
    std::atexit(clear_unclean_marker);
    std::filesystem::create_directories(root + name, ec);
    return root + name + "/";
}
#endif

static void log_message(AuroraLogLevel level, const char* module, const char* text, unsigned int length) {
    std::fprintf(stderr, "[%s] %.*s\n", module, static_cast<int>(length), text);
    if (level == LOG_FATAL) {
        void* frames[40];
        backtrace_symbols_fd(frames, backtrace(frames, 40), 2);
        std::abort();
    }
}

static std::string validateDisc(const std::string& path) {
    if (!aurora_dvd_open(path.c_str()))
        return "Could not read this image. Choose an ISO, GCM, CISO or RVZ file.";
    const auto* id = DVDGetCurrentDiskID();
    std::string error;
    if (std::memcmp(id->gameName, "GALE", 4) || std::memcmp(id->company, "01", 2))
        error = "This is a different game or region. Choose Super Smash Bros. Melee US 1.02.";
    else if (id->gameVersion != 2 || id->diskNumber != 0)
        error = "This Melee image is a different revision. The US 1.02 release is required.";
    else if (!MeleeLoadDiscFonts(path.c_str()))
        error = "This image is incomplete or damaged: its game fonts could not be loaded.";
    if (!error.empty()) aurora_dvd_close();
    return error;
}

int main(int argc, char** argv) {
#ifdef MELEE_MIYOO_FLIP
    if (argc != 2 || std::strcmp(argv[1], "--setup") == 0) {
        std::fprintf(stderr, "Usage: %s Melee-US-1.02-disc-image\n", argv[0]);
        return 2;
    }
    // SDL path (default): SDL owns the display, so let it pick the real video driver (KMSDRM).
    // DRM path (MELEE_FLIP_DISPLAY=drm): our own EGL/DRM code owns presentation, so SDL only
    // supplies controllers, audio and events under the dummy video driver.
    if (!MeleeFlipSdlDisplaySelected()) setenv("SDL_VIDEODRIVER", "dummy", 1);
#endif
    if (argc > 2) {
        std::fprintf(stderr, "Usage: %s [--setup | Melee-US-1.02-disc-image]\n", argv[0]);
        return 2;
    }
    const bool forceSetup = argc == 2 && std::strcmp(argv[1], "--setup") == 0;
    graphical_launch = argc == 1 || forceSetup;
    executable_path = argv[0];
    if (graphical_launch) {
        unsetenv("MELEE_INPUT_SCRIPT");
        unsetenv("MELEE_TEST_SEED");
        MeleePrepareAppLogging();
        // DVD file I/O is independent of Aurora's renderer, as in its DVD tests.
        // Finish setup before creating the game window.
        disc_path = MeleeLaunchDisc(validateDisc, forceSetup);
        if (disc_path.empty()) { aurora_dvd_close(); return 0; }
    } else {
        disc_path = argv[1];
        const auto error = validateDisc(disc_path);
        if (!error.empty()) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
    }
    std::fprintf(stderr, "[launch] Loaded GALE01 revision 2 from %s\n", disc_path.c_str());
    AuroraConfig config{};
    config.appName = "Melee Native";
#ifdef __APPLE__
    config.desiredBackend = BACKEND_METAL;
#elif defined(MELEE_MIYOO_FLIP)
    config.desiredBackend = BACKEND_OPENGLES;
#else
    config.desiredBackend = BACKEND_VULKAN;
#endif
#ifdef __linux__
    installCrashHandler();
    const auto user_path = MeleeConfigPath(), cache_path = pipelineCachePath(MeleeCachePath());
    config.userPath = user_path.c_str();
    config.cachePath = cache_path.c_str();
#endif
    config.vsync = true;
    config.windowWidth = 960;
    config.windowHeight = 720;
#ifdef MELEE_MIYOO_FLIP
    // DRM page flips provide the display cadence. Immediate mode asks Dawn to
    // set EGL's swap interval once when it creates the surface, avoiding a
    // second pacing queue and a per-frame eglSwapInterval workaround.
    config.vsync = false;
    config.allowJoystickBackgroundEvents = true;
    // Render at the panel's mode size: 640x480 on the Flip, the mode size elsewhere.
    MeleeFlipInitDisplay();
    {
        unsigned width = 640, height = 480;
        MeleeFlipDisplaySize(&width, &height);
        config.windowWidth = static_cast<int>(width);
        config.windowHeight = static_cast<int>(height);
    }
    // Upstream Aurora renderer options, on by default with the values validated on the device
    // (v143). Each has an environment override for A/B trials: MELEE_FLIP_<NAME>=0 disables,
    // =1 enables; the numeric ones take a value.
    const auto flag = [](const char* name, bool fallback) {
        const char* value = std::getenv(name);
        return value && *value ? std::strcmp(value, "0") != 0 : fallback;
    };
    const auto number = [](const char* name, unsigned long fallback) {
        const char* value = std::getenv(name);
        if (!value || !*value) return fallback;
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(value, &end, 10);
        return end && !*end ? parsed : fallback;
    };
    // Mali-G52 exposes no vertex-stage storage buffers, so Aurora's storage-buffer vertex path
    // cannot create its bind group layout here: MELEE_FLIP_CPU_VERTEX_DECODE=0 is expected to
    // fail at renderer initialization on this device and exists only to demonstrate that.
    config.cpuVertexDecode = flag("MELEE_FLIP_CPU_VERTEX_DECODE", true);
    config.residentDisplayLists = flag("MELEE_FLIP_RESIDENT_DL", true);
    config.residentGeometryBudget = static_cast<uint32_t>(number("MELEE_FLIP_RESIDENT_MB", 64) * 1024 * 1024);
    config.asyncFrames = flag("MELEE_FLIP_ASYNC_FIFO", true);
    config.textureVerifyInterval = static_cast<uint32_t>(number("MELEE_FLIP_TEXTURE_VERIFY_INTERVAL", 4));
    config.textureAtlas = flag("MELEE_FLIP_TEXTURE_ATLAS", true);
    config.disableRenderPassFusion = !flag("MELEE_FLIP_FUSE_PASSES", true);
    // GLES fast path (Aurora gles-direct-submission): uniform table + batching on every path,
    // direct GLES submission of GX passes through Dawn's native GL interop when available.
    config.uniformTable = flag("MELEE_FLIP_UNIFORM_TABLE", true);
    config.batchDraws = flag("MELEE_FLIP_BATCH_DRAWS", true);
    config.glesDirectSubmission = flag("MELEE_FLIP_DIRECT_GLES", true) ? 1 : -1;
    config.glesMappedStreams = flag("MELEE_FLIP_MAPPED_STREAM", true) ? 1 : -1;
    config.sortOpaqueDraws = flag("MELEE_FLIP_SORT_OPAQUE", false);
    config.residentRecords = flag("MELEE_FLIP_RESIDENT_RECORDS", false);
    // Internal render-resolution divisor: 0 auto (quarter-res only on >=1080p panels), 1 native, 2 half,
    // 4 quarter. Default 0 so high-res desktops/TVs (Pi on a monitor) get the relief and handhelds stay full.
    {
        const long rs = number("MELEE_FLIP_RENDER_SCALE", 0);
        config.renderScale = rs >= 4 ? 4u : rs >= 2 ? 2u : rs == 1 ? 1u : 0u;
    }
    config.renderStats = std::getenv("MELEE_FLIP_PROFILE") != nullptr;
    config.sceneOnSurface = flag("MELEE_FLIP_SCENE_ON_SURFACE", true);
    config.halfResolutionSpritePoints = static_cast<uint32_t>(number("MELEE_FLIP_HALFRES_SPRITES", 4000));
    config.smallCopyPassInterval = static_cast<uint32_t>(number("MELEE_FLIP_SCALED_COPY_INTERVAL", 2));
    // Readback checks need the scene mirrored into the EFB texture when it renders on the surface.
    if (std::getenv("MELEE_RENDER_CHECK") || std::getenv("MELEE_MATRIX_TEST")) setenv("AURORA_SCENE_MIRROR", "1", 0);
    std::fprintf(stderr, "[flip-config] cpu_vertex_decode=%d resident_dl=%d resident_budget_mb=%u async_frames=%d texture_verify_interval=%u texture_atlas=%d pass_fusion=%d uniform_table=%d batch_draws=%d gles_direct=%d mapped_streams=%d scene_on_surface=%d halfres_sprites=%u small_copy_interval=%u resident_records=%d render_scale=%u render_stats=%d\n",
                 config.cpuVertexDecode, config.residentDisplayLists, config.residentGeometryBudget / (1024u * 1024u),
                 config.asyncFrames, config.textureVerifyInterval, config.textureAtlas, !config.disableRenderPassFusion,
                 config.uniformTable, config.batchDraws, config.glesDirectSubmission, config.glesMappedStreams,
                 config.sceneOnSurface, config.halfResolutionSpritePoints, config.smallCopyPassInterval, config.residentRecords, config.renderScale, config.renderStats);
#endif
    config.logCallback = log_message;
    config.mem1Size = MEM1_DEFAULT_SIZE;
    config.mem2Size = ARAM_DEFAULT_SIZE;
    const auto info = aurora_initialize(argc, argv, &config);
    if (std::getenv("MELEE_MATRIX_TEST")) {
        if (const auto* test = std::getenv("MELEE_TEST_CASE")) {
            const std::string title = std::string("Melee test: ") + test;
            SDL_SetWindowTitle(info.window, title.c_str());
        }
    }
    MeleeNativeConfigureOS(info.userPath, reset_game);
    MeleeNativeSettingsLoad(info.userPath);
    MeleeNativeNetplayInit(info.userPath);
#ifdef __APPLE__
    MeleeNativeSkipSavePrompt = graphical_launch;
    if (graphical_launch) {
        MeleeInstallAppMenu([] {
            char setup[] = "--setup";
            char* arguments[] = {executable_path.data(), setup, nullptr};
            execv(arguments[0], arguments);
            std::perror("Could not restart setup");
        });
        SDL_RaiseWindow(info.window);
    }
#endif
    const int result = melee_game_main();
    aurora_dvd_close();
    aurora_shutdown();
    return result;
}
