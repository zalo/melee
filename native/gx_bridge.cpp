#include "gx_bridge.h"
#include <dolphin/gx.h>
#include <dolphin/os.h>
#include <dolphin/card.h>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>

namespace {
struct Region { std::size_t size; bool little; };
std::map<std::uintptr_t, Region> regions;
std::mutex region_mutex;
}
extern "C" void MeleeNativeRegisterVertexBuffer(const void* data, size_t size, int little) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    if (!data || !size || size > std::numeric_limits<std::uintptr_t>::max() - address)
        OSPanic(__FILE__, __LINE__, "Invalid native vertex buffer range");
    std::lock_guard lock(region_mutex);
    auto next = regions.lower_bound(address);
    if ((next != regions.end() && next->first < address + size) ||
        (next != regions.begin() && std::prev(next)->first + std::prev(next)->second.size > address))
        OSPanic(__FILE__, __LINE__, "Overlapping native vertex buffers");
    regions.emplace(address, Region{size, little != 0});
}
extern "C" void MeleeNativeReleaseResidentGeometry(const void* data, std::size_t size);
extern "C" void MeleeNativeUnregisterVertexBuffer(const void* data) {
    std::size_t size = 0;
    {
        std::lock_guard lock(region_mutex);
        const auto it = regions.find(reinterpret_cast<std::uintptr_t>(data));
        if (it == regions.end())
            OSPanic(__FILE__, __LINE__, "Unknown native vertex buffer");
        size = it->second.size;
        regions.erase(it);
    }
    // Resident geometry decoded from this region must not outlive it. The
    // notice travels through the GX stream so it is ordered against draws.
    MeleeNativeReleaseResidentGeometry(data, size);
}
extern "C" void MeleeNativeSetArrayData(int attribute, const void* data, unsigned size,
                                        unsigned char stride, int little) {
    if (!data || !stride || size < stride)
        OSPanic(__FILE__, __LINE__, "Invalid native vertex array");
    GXSetArray(static_cast<GXAttr>(attribute), data, size, stride, little != 0);
}
extern "C" void MeleeGXSetArray(GXAttr attribute, const void* data, u8 stride) {
    std::lock_guard lock(region_mutex);
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    auto region = regions.upper_bound(address);
    if (region == regions.begin())
        OSPanic(__FILE__, __LINE__, "Vertex array is not backed by a registered native asset");
    --region;
    const auto offset = address - region->first;
    if (offset >= region->second.size || region->second.size - offset > UINT32_MAX)
        OSPanic(__FILE__, __LINE__, "Native vertex array exceeds registered bounds");
    MeleeNativeSetArrayData(attribute, data, static_cast<unsigned>(region->second.size - offset),
                            stride, region->second.little);
}

extern "C" {
GXRenderModeObj GXNtsc480Prog = {
    static_cast<VITVMode>(2), 640, 480, 480, 40, 0, 640, 480, static_cast<VIXFBMode>(0), 0, 0,
    {{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6}},
    {0,0,21,22,21,0,0}
};
}

extern "C" void MeleeNativeCARDInit(void) { CARDInit("GALE", "01"); }

#include "__gx.h"
#include "dolphin/gx/GXAurora.h"
#include <cmath>
extern "C" void GXSetCopyClamp(GXFBClamp clamp) {
    // Native display output uses the EFB directly. Texture copies still carry
    // the original clamp bits in their BP control register.
    __gx->cpTex = (__gx->cpTex & ~3U) | (static_cast<u32>(clamp) & 3U);
}
extern "C" void GXSetMisc(GXMiscToken token, u32 value) {
    switch (token) {
    case GX_MT_NULL: break;
    case GX_MT_XF_FLUSH:
        __gx->vNum = static_cast<u16>(value);
        __gx->bpSent = 1;
        __gx->dirtyState |= 8;
        break;
    case GX_MT_DL_SAVE_CONTEXT:
        __gx->dlSaveContext = value != 0;
        break;
    default: OSPanic(__FILE__, __LINE__, "Invalid GX miscellaneous token");
    }
}
extern "C" void GXWaitDrawDone(void) {
    if (aurora::gx::fifo::async_frames()) aurora::gx::fifo::wait_draw_done();
    else aurora::gx::fifo::drain();
}
extern "C" void MeleeNativeReleaseResidentGeometry(const void* data, std::size_t size) {
    GX_WRITE_AURORA(GX_AURORA_INVALIDATE_RESIDENT);
    GX_WRITE_U64(reinterpret_cast<u64>(data));
    GX_WRITE_U32(static_cast<u32>(size));
    aurora::gx::fifo::publish();
}
extern "C" void GXSetTevClampMode(int, int) {
    // The original retail SDK implementation is empty; its debug build asserts
    // that this obsolete call is unavailable. Actual TEV clamp is set per op.
}
extern "C" void GXInitFogAdjTable(GXFogAdjTable* table, u16 width, f32 projection[4][4]) {
    if (!table || !width || width > 640) OSPanic(__FILE__, __LINE__, "Invalid fog adjustment table");
    float near_z, side_x;
    if (projection[3][3] == 0.0f) {
        near_z = projection[2][3] / (projection[2][2] - 1.0f);
        side_x = near_z * (1.0f + projection[0][2]) / projection[0][0];
    } else {
        near_z = (1.0f + projection[2][3]) / projection[2][2];
        side_x = -(projection[0][3] - 1.0f) / projection[0][0];
    }
    const auto inverse_width = 2.0f / width;
    for (unsigned i = 0; i < 10; ++i) {
        float x = static_cast<float>((i+1)*32);
        x *= inverse_width; x *= side_x;
        table->r[i] = static_cast<u32>(256.0f * std::sqrt(1.0f + x*x/(near_z*near_z))) & 0xFFF;
    }
}
