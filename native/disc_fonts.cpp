#include "disc_fonts.h"
#include <nod.h>
#include <cstdint>
#include <cstring>
#include <memory>

extern "C" {
extern uint8_t HSD_DebugFontAtlas[0x1C00];
extern uint8_t HSD_SisLib_FontAtlas[0x23E00];
}

namespace {
uint32_t be32(const uint8_t* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 |
           uint32_t(p[2]) << 8 | p[3];
}

bool copyData(const NodBlob& dol, uint32_t address, void* destination, size_t size) {
    if (!dol.data || dol.size < 0x100) return false;
    // Seven text and eleven data sections share parallel DOL header arrays.
    for (size_t i = 0; i < 18; ++i) {
        const uint32_t offset = be32(dol.data + i * 4);
        const uint32_t start = be32(dol.data + 0x48 + i * 4);
        const uint32_t length = be32(dol.data + 0x90 + i * 4);
        if (address < start || uint64_t(address - start) + size > length) continue;
        const uint64_t source = uint64_t(offset) + address - start;
        if (source > dol.size || size > dol.size - source) return false;
        std::memcpy(destination, dol.data + source, size);
        return true;
    }
    return false;
}
}

bool MeleeLoadDiscFonts(const char* path) {
    NodHandle* raw = nullptr;
    if (nod_disc_open(path, nullptr, &raw) != NOD_RESULT_OK) return false;
    std::unique_ptr<NodHandle, decltype(&nod_free)> disc(raw, nod_free);
    if (nod_disc_open_partition(disc.get(), 0, nullptr, &raw) != NOD_RESULT_OK) return false;
    std::unique_ptr<NodHandle, decltype(&nod_free)> partition(raw, nod_free);
    NodPartitionMeta meta{};
    if (nod_partition_meta(partition.get(), &meta) != NOD_RESULT_OK) return false;
    return copyData(meta.raw_dol, 0x804088B8, HSD_DebugFontAtlas, sizeof(HSD_DebugFontAtlas)) &&
           copyData(meta.raw_dol, 0x8040CD40, HSD_SisLib_FontAtlas, sizeof(HSD_SisLib_FontAtlas));
}
