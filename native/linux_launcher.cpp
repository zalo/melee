#include "platform_launcher.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

static std::string xdgPath(const char* variable, const char* fallback) {
    const char* base = std::getenv(variable);
    std::filesystem::path path;
    if (base && base[0] == '/') path = base;
    else {
        const char* home = std::getenv("HOME");
        if (!home || home[0] != '/') throw std::runtime_error("HOME must be an absolute path");
        path = std::filesystem::path(home) / fallback;
    }
    path /= "melee-native";
    std::filesystem::create_directories(path);
    return path.string() + "/";
}
std::string MeleeConfigPath() { return xdgPath("XDG_CONFIG_HOME", ".config"); }
std::string MeleeCachePath() { return xdgPath("XDG_CACHE_HOME", ".cache"); }

void MeleePrepareAppLogging() {
    try {
        const auto path = xdgPath("XDG_STATE_HOME", ".local/state") + "game.log";
        if (!std::freopen(path.c_str(), "w", stderr)) return;
        std::setvbuf(stderr, nullptr, _IONBF, 0);
    } catch (const std::exception& e) { std::fprintf(stderr, "Logging: %s\n", e.what()); }
}
void MeleeShowLaunchError(const std::string& error) {
    std::fprintf(stderr, "[launch-error] %s\n", error.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Melee Native — could not load disc", error.c_str(), nullptr);
}
std::string MeleeChooseDisc(const std::string& error) {
    // The portal response arrives asynchronously; keep the main-thread SDL
    // event pump running until the callback has published its result.
    SDL_SetAppMetadata("Melee Native", "0.1.0", "org.melee.native");
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "Video initialization failed: %s\n", SDL_GetError());
        return {};
    }
    if (!error.empty()) MeleeShowLaunchError(error);
    struct Selection { std::string path; std::atomic<bool> done{false}; } selection;
    const SDL_DialogFileFilter filters[] = {{"GameCube disc image", "iso;gcm;ciso;rvz"}, {"All files", "*"}};
    SDL_ShowOpenFileDialog([](void* data, const char* const* files, int) {
        auto& result = *static_cast<Selection*>(data);
        if (files && files[0]) result.path = files[0];
        else if (!files) std::fprintf(stderr, "File dialog failed: %s\n", SDL_GetError());
        result.done.store(true, std::memory_order_release);
    }, &selection, nullptr, filters, 2, nullptr, false);
    while (!selection.done.load(std::memory_order_acquire)) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {}
        SDL_Delay(10);
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return selection.path;
}
