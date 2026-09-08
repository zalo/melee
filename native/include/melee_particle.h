#pragma once
#include <stdint.h>
// Relocated host representation shared by the archive reader and HSD runtime.
#define MELEE_PARTICLE_BANK_MAGIC 0x5053424eU
typedef struct MeleeNativeParticleBank {
    uint32_t magic;
    uint32_t count;
    void** entries;
} MeleeNativeParticleBank;
