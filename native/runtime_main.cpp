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
#include <SDL3/SDL.h>
#include "disc_fonts.h"

static std::string executable_path, disc_path;
static void reset_game(int type) {
    if (type == 2) std::exit(0);
    char* arguments[] = {executable_path.data(), disc_path.data(), nullptr};
    execv(arguments[0], arguments);
    std::perror("Native restart failed"); std::abort();
}

extern "C" int melee_game_main(void);

static void log_message(AuroraLogLevel level, const char* module, const char* text, unsigned int length) {
    std::fprintf(stderr, "[%s] %.*s\n", module, static_cast<int>(length), text);
    if (level == LOG_FATAL) {
        void* frames[40];
        backtrace_symbols_fd(frames, backtrace(frames, 40), 2);
        std::abort();
    }
}

int main(int argc, char** argv) {
    if (argc > 2) {
        std::fprintf(stderr, "Usage: melee_native [Melee-US-1.02-disc-image]\n");
        return 2;
    }
    const bool graphical_launch = argc == 1;
    executable_path = argv[0];
    if (graphical_launch) {
        unsetenv("MELEE_INPUT_SCRIPT");
        unsetenv("MELEE_TEST_SEED");
        MeleePrepareAppLogging();
        disc_path = MeleeChooseDisc();
        if (disc_path.empty()) return 0;
    } else disc_path = argv[1];
    AuroraConfig config{};
    config.appName = "Melee Native";
#ifdef __APPLE__
    config.desiredBackend = BACKEND_METAL;
#else
    config.desiredBackend = BACKEND_VULKAN;
#endif
#ifdef __linux__
    const auto user_path = MeleeConfigPath(), cache_path = MeleeCachePath();
    config.userPath = user_path.c_str();
    config.cachePath = cache_path.c_str();
#endif
    config.vsync = true;
    config.windowWidth = 960;
    config.windowHeight = 720;
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
    for (;;) {
        std::string error;
        if (!aurora_dvd_open(disc_path.c_str())) {
            error = "The file could not be read as a supported GameCube disc image. Choose an ISO, GCM, CISO or RVZ file.";
        } else {
            const auto* id = DVDGetCurrentDiskID();
            if (std::memcmp(id->gameName, "GALE", 4) || std::memcmp(id->company, "01", 2) ||
                id->gameVersion != 2 || id->diskNumber != 0)
                error = "This port requires Super Smash Bros. Melee US revision 1.02 (GALE01, revision 2). This image is a different game or version.";
        }
        if (error.empty() && !MeleeLoadDiscFonts(disc_path.c_str()))
            error = "Could not load the font data from this disc's main.dol.";
        if (error.empty()) {
            std::fprintf(stderr, "[launch] Loaded GALE01 revision 2 from %s\n", disc_path.c_str());
            break;
        }
        aurora_dvd_close();
        std::fprintf(stderr, "%s\n", error.c_str());
        if (!graphical_launch) { aurora_shutdown(); return 1; }
        disc_path = MeleeChooseDisc(error);
        if (disc_path.empty()) { aurora_shutdown(); return 0; }
    }
    const int result = melee_game_main();
    aurora_dvd_close();
    aurora_shutdown();
    return result;
}
