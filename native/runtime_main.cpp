#include <aurora/aurora.h>
#include <aurora/dvd.h>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <execinfo.h>
#include "os_runtime.h"
#include "mac_launcher.h"
#include <dolphin/dvd.h>
#include <cstring>
#include <SDL3/SDL.h>
#include "disc_fonts.h"

static std::string executable_path, disc_path;
static bool graphical_launch;
static void reset_game(int type) {
    if (type == 2) std::exit(0);
    char* arguments[] = {executable_path.data(), graphical_launch ? nullptr : disc_path.data(), nullptr};
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
    if (argc > 2) {
        std::fprintf(stderr, "Usage: melee_mac [--setup | Melee-US-1.02-disc-image]\n");
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
    config.desiredBackend = BACKEND_METAL;
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
    const int result = melee_game_main();
    aurora_dvd_close();
    aurora_shutdown();
    return result;
}
