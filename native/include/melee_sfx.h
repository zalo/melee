#pragma once
#include <dolphin/ax.h>
#include <stddef.h>
/* Runtime entries hold a native pointer followed by integer metadata and
 * 64-byte voice descriptors. Serialized data is never overlaid on this type. */
typedef struct MeleeSfxEntry {
    struct MeleeSfxEntry* next;
    s32 id, count, rate;
    AXPBADDR addr;
    AXPBADPCM adpcm;
    AXPBADPCMLOOP loop;
} MeleeSfxEntry;
u32 MeleeSfxRead32(const void* data);
size_t MeleeSfxGroupSize(const u8* first16, const u8* rest, size_t size, u32 count);
int MeleeSfxBuildGroup(AXVPB* group, size_t capacity, const u8* first16, const u8* rest,
    size_t size, u32 count, u32 base, u32 aram, u32 aram_size, int entry, void** buckets);
size_t MeleeSfxEntrySize(u32 voices);
