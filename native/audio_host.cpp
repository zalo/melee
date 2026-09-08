#include "audio_host.h"
#include <SDL3/SDL.h>
#include <dolphin/ar.h>
#include <cstdio>
#include <cstdlib>

static SDL_AudioStream* output;
static void (*renderer)(int16_t*, unsigned);
static void fill(void*, SDL_AudioStream* stream, int additional, int) {
    // AX callbacks operate on 160 samples at 32 kHz, independent of the device.
    while (additional > 0) {
        int16_t block[320];
        renderer(block, 160);
        if (!SDL_PutAudioStreamData(stream, block, sizeof(block))) {
            std::fprintf(stderr, "Audio stream failed: %s\n", SDL_GetError());
            std::abort();
        }
        additional -= sizeof(block);
    }
}
extern "C" void MeleeNativeAudioOpen(void (*render)(int16_t*, unsigned)) {
    if (output) return;
    renderer = render;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "Audio initialization failed: %s\n", SDL_GetError());
        std::abort();
    }
    const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, 32000};
    output = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, fill, nullptr);
    if (!output || !SDL_ResumeAudioStreamDevice(output)) {
        std::fprintf(stderr, "Audio device failed: %s\n", SDL_GetError());
        std::abort();
    }
}
extern "C" void MeleeNativeAudioClose(void) {
    SDL_DestroyAudioStream(output);
    output = nullptr;
}
extern "C" void* MeleeNativeARAM(unsigned* size) {
    *size = ARGetSize();
    return ARGetStorageAddress();
}
