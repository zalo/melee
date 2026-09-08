#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void MeleeNativeAudioOpen(void (*render)(int16_t*, unsigned));
void MeleeNativeAudioClose(void);
void* MeleeNativeARAM(unsigned* size);
#ifdef __cplusplus
}
#endif
