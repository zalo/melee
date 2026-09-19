#include "audio_host.h"
#include <SDL3/SDL.h>
#include <dolphin/ar.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static SDL_AudioStream* output;
static void (*renderer)(int16_t*, unsigned);
static void fill(void*, SDL_AudioStream* stream, int additional, int) {
    // AX callbacks operate on 160 samples at 32 kHz, independent of the device.
    while (additional > 0) {
        int16_t block[320];
        renderer(block, 160);
        static const bool check = std::getenv("MELEE_AUDIO_CHECK") != nullptr;
        if (check) {
            static unsigned frames = 0, nonzero = 0, peak = 0;
            for (auto value : block) {
                const unsigned magnitude = value < 0 ? -int(value) : int(value);
                if (magnitude) ++nonzero;
                if (magnitude > peak) peak = magnitude;
            }
            frames += 160;
            if (frames >= 160000) {
                std::fprintf(stderr, "[audio-check] frames=%u nonzero_samples=%u peak=%u\n", frames, nonzero, peak);
                frames = nonzero = peak = 0;
            }
        }
        if (!SDL_PutAudioStreamData(stream, block, sizeof(block))) {
            std::fprintf(stderr, "Audio stream failed: %s\n", SDL_GetError());
            std::abort();
        }
        additional -= sizeof(block);
    }
}
// Bring up SDL audio. The CFW's environment may name a backend (ROCKNIX exports
// SDL_AUDIODRIVER=pulseaudio system-wide, SDL3 also reads it through SDL_AUDIO_DRIVER); honour it,
// and when that backend is not available in this build or on this device, fall back to SDL's own
// order (pipewire, pulseaudio, alsa) instead of running silent.
static bool initAudioSubsystem() {
    // With the SDL3-over-SDL2 shim the launcher sets SDL_AUDIODRIVER=sdl2 to pick the shim's audio
    // driver; the CFW's SDL2 inside the shim reads the same variable and would reject the name unless
    // SDL3SHIM_SDL2_AUDIODRIVER replaces it. Keep the choice as an SDL3 hint and drop the variable so
    // the inner SDL2 opens its own default backend (see initSdlVideo in platform/flip/display.cpp).
    const char* fromEnv = std::getenv("SDL_AUDIODRIVER");
    const char* named = SDL_GetHint(SDL_HINT_AUDIO_DRIVER);
    if ((fromEnv && !std::strcmp(fromEnv, "sdl2")) || (named && !std::strcmp(named, "sdl2"))) {
        SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "sdl2", SDL_HINT_OVERRIDE);
        unsetenv("SDL_AUDIODRIVER");
        unsetenv(SDL_HINT_AUDIO_DRIVER);
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) return true;
    const char* wanted = SDL_GetHint(SDL_HINT_AUDIO_DRIVER);
    if (!wanted || !*wanted) return false;
    std::fprintf(stderr, "[audio] driver '%s' from the environment is unavailable (%s); trying the others\n",
                 wanted, SDL_GetError());
    unsetenv("SDL_AUDIODRIVER");
    unsetenv(SDL_HINT_AUDIO_DRIVER);
    SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "", SDL_HINT_OVERRIDE);
    return SDL_InitSubSystem(SDL_INIT_AUDIO);
}
extern "C" void MeleeNativeAudioOpen(void (*render)(int16_t*, unsigned)) {
    if (output) return;
    renderer = render;
    // Audio must never be fatal: on shared handhelds (PortMaster) the sound device
    // can be briefly held by the launcher/menu when a port starts. Retry a few
    // times, then run without audio rather than aborting the whole game. The game
    // loop is driven by video, not by the audio pull, so silent operation is safe.
    if (!initAudioSubsystem()) {
        std::fprintf(stderr, "Audio initialization failed, continuing without sound: %s\n", SDL_GetError());
        return;
    }
    const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, 32000};
    for (int attempt = 0; attempt < 10; ++attempt) {
        output = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, fill, nullptr);
        if (output && SDL_ResumeAudioStreamDevice(output)) {
            std::fprintf(stderr, "[audio] driver=%s device=%s\n", SDL_GetCurrentAudioDriver(),
                         SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(output)));
            return;
        }
        if (output) {
            SDL_DestroyAudioStream(output);
            output = nullptr;
        }
        SDL_Delay(200);
    }
    std::fprintf(stderr, "Audio device unavailable after retries, continuing without sound: %s\n", SDL_GetError());
}
extern "C" void MeleeNativeAudioClose(void) {
    SDL_DestroyAudioStream(output);
    output = nullptr;
}
extern "C" void* MeleeNativeARAM(unsigned* size) {
    *size = ARGetSize();
    return ARGetStorageAddress();
}
