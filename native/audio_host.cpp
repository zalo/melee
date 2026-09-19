#include "audio_host.h"
#include <SDL3/SDL.h>
#include <dolphin/ar.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static SDL_AudioStream* output;
static void (*renderer)(int16_t*, unsigned);

// Deterministic mode (online play, MELEE_DETERMINISTIC_IO): the AX frame callback and the mixer run on
// the game thread, ten 5 ms frames per three logic frames (MeleeNativeAudioTick), so voice
// allocation, voice-end callbacks and everything else the game reads back from the audio engine
// advance as a function of simulated frames instead of the audio clock. The audio thread only drains
// the mixed blocks; when the game runs below 60 Hz the ring runs dry and the output has gaps.
extern "C" int MeleeNativeDeterministicIO(void) __attribute__((weak));
static bool deterministic_audio() {
    static const bool value = MeleeNativeDeterministicIO && MeleeNativeDeterministicIO();
    return value;
}
namespace {
constexpr unsigned kRingBlocks = 64; // 320 ms
int16_t ring[kRingBlocks][320];
std::atomic<unsigned> ring_head{0}, ring_tail{0}; // producer: game thread, consumer: audio thread
}
extern "C" void MeleeNativeAudioTick(void) {
    if (!renderer || !deterministic_audio()) return;
    static unsigned phase = 0;
    const unsigned frames = (phase++ % 3 == 2) ? 4 : 3;
    for (unsigned i = 0; i < frames; ++i) {
        const unsigned head = ring_head.load(std::memory_order_relaxed);
        if (head - ring_tail.load(std::memory_order_acquire) < kRingBlocks) {
            renderer(ring[head % kRingBlocks], 160);
            ring_head.store(head + 1, std::memory_order_release);
        } else {
            int16_t dropped[320]; // the engine must still advance
            renderer(dropped, 160);
        }
    }
}
static void fill(void*, SDL_AudioStream* stream, int additional, int) {
    // AX callbacks operate on 160 samples at 32 kHz, independent of the device.
    while (additional > 0) {
        int16_t block[320];
        if (deterministic_audio()) {
            const unsigned tail = ring_tail.load(std::memory_order_relaxed);
            if (ring_head.load(std::memory_order_acquire) != tail) {
                std::memcpy(block, ring[tail % kRingBlocks], sizeof block);
                ring_tail.store(tail + 1, std::memory_order_release);
            } else {
                std::memset(block, 0, sizeof block);
            }
        } else {
            renderer(block, 160);
        }
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
