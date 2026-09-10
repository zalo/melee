#include "display.h"
#include "webgpu/gpu.hpp"
#include "gfx/render_worker.hpp"
#include <array>
#include <algorithm>
#include <aurora/aurora.h>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <cstdio>
#include <atomic>
#include <filesystem>
static std::atomic<bool> failed{false};
#include <cstdlib>
#include <cstring>

static void log(AuroraLogLevel level, const char* module, const char* text, unsigned len) {
    std::fprintf(stderr, "[%s] %.*s\n", module, int(len), text);
    if (level >= LOG_ERROR) failed = true;
    if (level == LOG_FATAL) std::abort();
}
static void checkFrame(unsigned phase, unsigned multiDraws) {
    aurora::gfx::render_worker::enqueue_work([phase, multiDraws] {
        using namespace aurora::webgpu;
        const auto& source = present_source();
        const unsigned width = source.size.width, height = source.size.height;
        const unsigned pitch = (width * 4 + 255) & ~255U;
        const uint64_t size = uint64_t(pitch) * height;
        const wgpu::BufferDescriptor desc{.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead, .size = size};
        auto buffer = g_device.CreateBuffer(&desc);
        auto encoder = g_device.CreateCommandEncoder();
        const wgpu::TexelCopyTextureInfo input{.texture = source.texture};
        const wgpu::TexelCopyBufferInfo output{.layout = {.bytesPerRow = pitch}, .buffer = buffer};
        const wgpu::Extent3D extent{width, height, 1};
        encoder.CopyTextureToBuffer(&input, &output, &extent);
        auto commands = encoder.Finish();
        g_queue.Submit(1, &commands);
        const auto future = buffer.MapAsync(wgpu::MapMode::Read, 0, size, wgpu::CallbackMode::WaitAnyOnly,
            [buffer, size, width, height, pitch, phase, multiDraws](wgpu::MapAsyncStatus status, wgpu::StringView) {
                if (status != wgpu::MapAsyncStatus::Success) { failed = true; return; }
                auto* pixels = static_cast<const unsigned char*>(buffer.GetConstMappedRange(0, size));
                std::array<bool, 4096> colors{};
                for (unsigned y = 0; y < height; y += 8)
                    for (unsigned x = 0; x < width; x += 8) {
                        const auto* p = pixels + y * pitch + x * 4;
                        colors[(p[0] >> 4) * 256 + (p[1] >> 4) * 16 + (p[2] >> 4)] = true;
                    }
                const auto count = std::count(colors.begin(), colors.end(), true);
                uint64_t hash = 1469598103934665603ULL;
                for (unsigned i = 0; i < size; ++i) hash = (hash ^ pixels[i]) * 1099511628211ULL;
                static uint64_t previous;
                std::fprintf(stderr, "[flip-probe] phase=%u image hash=%016llx\n", phase + 1, (unsigned long long)hash);
                if (previous && previous == hash) failed = true;
                previous = hash;
                std::fprintf(stderr, "[flip-probe] framebuffer %ux%u, distinct colors=%zd\n", width, height, count);
                if (count < 50) failed = true;
                const auto lit = [&](unsigned x) {
                    const auto* p = pixels + (height / 2) * pitch + x * 4;
                    return std::max({p[0], p[1], p[2]}) > 50;
                };
                if (phase < 4) {
                    if (!lit(width / 2)) failed = true;
                } else if (phase >= 5) {
                    for (unsigned i = 0; i < multiDraws; ++i) {
                        if (!lit(width * (2 * i + 1) / (2 * multiDraws))) {
                            failed = true;
                            std::fprintf(stderr, "[flip-probe] missing transformed draw %u\n", i + 1);
                        }
                    }
                    if (multiDraws % 2 == 0 && lit(width / 2)) failed = true;
                } else if (!lit(width / 4) || lit(width / 2) || lit(width * 3 / 4)) {
                    failed = true;
                    std::fprintf(stderr, "[flip-probe] transformed draw coverage failed\n");
                }
                if (auto* out = std::fopen("/tmp/melee-flip-probe/frame.ppm", "wb")) {
                    std::fprintf(out, "P6\n%u %u\n255\n", width, height);
                    for (unsigned y = 0; y < height; ++y)
                        for (unsigned x = 0; x < width; ++x) std::fwrite(pixels + y * pitch + x * 4, 1, 3, out);
                    std::fclose(out);
                }
                buffer.Unmap();
            });
        if (g_instance.WaitAny(future, 5000000000) != wgpu::WaitStatus::Success) failed = true;
    });
    aurora::gfx::render_worker::synchronize();
}
int main(int argc, char** argv) {
    std::filesystem::create_directories("/tmp/melee-flip-probe/cache");
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    MeleeFlipInitDisplay();
    AuroraConfig config{};
    config.appName = "Melee Flip GPU probe";
    config.userPath = "/tmp/melee-flip-probe/";
    config.cachePath = "/tmp/melee-flip-probe/cache/";
    config.desiredBackend = BACKEND_OPENGLES;
    config.windowWidth = 640;
    config.windowHeight = 480;
    config.logCallback = log;
    config.vsync = true;
    const auto info = aurora_initialize(argc, argv, &config);
    if (info.backend != BACKEND_OPENGLES) return 1;
    GXInit(nullptr, 0);
    if (argc == 2 && std::strcmp(argv[1], "--present") == 0) {
        std::array<std::array<float, 3>, 346> positions{};
        positions[343] = {-0.8f, -0.8f, -1};
        positions[344] = {0.8f, -0.8f, -1};
        positions[345] = {0, 0.8f, -1};
        std::array<unsigned char, 64> white{};
        white.fill(255);
        GXTexObj texture;
        GXInitTexObj(&texture, white.data(), 4, 4, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
        GXInitTexObjLOD(&texture, GX_NEAR, GX_NEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
        auto bigPositions = positions;
        for (auto& vertex : bigPositions) for (auto& value : vertex) {
            uint32_t bits;
            std::memcpy(&bits, &value, sizeof(bits));
            bits = __builtin_bswap32(bits);
            std::memcpy(&value, &bits, sizeof(bits));
        }
        const char* requestedDraws = std::getenv("MELEE_PROBE_DRAWS");
        const unsigned multiDraws = requestedDraws ? std::strtoul(requestedDraws, nullptr, 10) : 2;
        if (multiDraws < 2 || multiDraws > 16) return 2;
        const char* requestedFrames = std::getenv("MELEE_PROBE_FRAMES");
        const unsigned framesPerPhase = requestedFrames ? std::strtoul(requestedFrames, nullptr, 10) : 60;
        if (framesPerPhase < 1 || framesPerPhase > 60) return 2;
        for (unsigned step = 0; step < 7 * framesPerPhase; ++step) {
            const unsigned phase = step / framesPerPhase;
            const unsigned frame = phase * 60 + step % framesPerPhase;
            aurora_update();
            if (!aurora_begin_frame()) return 2;
            GXSetCopyClear({16, 24, 40, 255}, GX_MAX_Z24);
            GXSetViewport(0, 0, 640, 480, 0, 1);
            GXSetScissor(0, 0, 640, 480);
            Mtx44 projection;
            C_MTXOrtho(projection, 1, -1, -1, 1, 0, 10);
            GXSetProjection(projection, GX_ORTHOGRAPHIC);
            Mtx model;
            C_MTXIdentity(model);
            GXLoadPosMtxImm(model, GX_PNMTX0);
            GXSetCurrentMtx(GX_PNMTX0);
            GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
            GXSetColorUpdate(GX_TRUE);
            GXSetAlphaUpdate(GX_TRUE);
            GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
            GXSetCullMode(GX_CULL_NONE);
            GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY);
            GXSetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);
            GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
            GXSetNumChans(1);
            GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
            GXSetNumTexGens(0);
            GXSetNumTevStages(1);
            GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
            GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
            if (frame >= 120) {
                GXLoadTexObj(&texture, GX_TEXMAP0);
                GXSetNumTexGens(1);
                GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
                GXSetTevOp(GX_TEVSTAGE0, GX_MODULATE);
                GXSetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
                GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
            }
            GXClearVtxDesc();
            GXSetVtxDesc(GX_VA_POS, frame < 60 ? GX_DIRECT : GX_INDEX16);
            const bool bigEndian = frame >= 180 && frame < 240;
            GXSetArray(GX_VA_POS, bigEndian ? bigPositions.data() : positions.data(),
                       sizeof(positions), sizeof(positions[0]), !bigEndian);
            GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
            GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
            GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
            if (frame >= 120) {
                GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
                GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
            }
            for (unsigned draw = 0; draw < (frame >= 300 ? multiDraws : 1U); ++draw) {
                if (frame >= 240) {
                    C_MTXIdentity(model);
                    model[0][0] = frame >= 300 ? 0.9f / multiDraws : 0.45f;
                    model[0][3] = frame >= 300 ? -1.f + (2.f * draw + 1.f) / multiDraws : -0.5f;
                    GXLoadPosMtxImm(model, GX_PNMTX0);
                }
                GXBegin(frame >= 360 ? GX_TRIANGLESTRIP : GX_TRIANGLES, GX_VTXFMT0, 3);
                for (unsigned i = 0; i < 3; ++i) {
                    if (frame < 60) GXPosition3f32(positions[343+i][0], positions[343+i][1], positions[343+i][2]);
                    else GXPosition1x16(343+i);
                    const unsigned color = (i + frame / 60) % 3;
                    GXColor4u8(color == 0 ? 255 : 32, color == 1 ? 255 : 32, color == 2 ? 255 : 32, 255);
                    if (frame >= 120) GXTexCoord2f32(i == 1 ? 1 : 0, i == 2 ? 1 : 0);
                }
                GXEnd();
            }
            aurora_end_frame();
            if (step % framesPerPhase == framesPerPhase - 1) checkFrame(phase, multiDraws);
        }
    }
    aurora_shutdown();
    if (failed) return 1;
    std::fprintf(stderr, "PASS: Flip renderer probe\n");
    return 0;
}
