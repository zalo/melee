// Platform diagnostic, deliberately separate from the game executable.
#include "asset_archive.hpp"
#include <aurora/aurora.h>
#include <aurora/event.h>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <stdexcept>

extern "C" void MeleeNativeEmitQuad(void);

static void log_message(AuroraLogLevel level, const char* module, const char* text, unsigned length) {
    std::fprintf(stderr, "[%s] %.*s\n", module, static_cast<int>(length), text);
    if (level == LOG_FATAL) std::abort();
}
struct Texture { std::uint32_t image, palette; };
static std::vector<Texture> textures(const melee::AssetArchive& archive) {
    std::vector<Texture> result;
    std::set<std::uint32_t> joints, images;
    auto visit = [&](auto&& self, std::uint32_t joint) -> void {
        if (!joints.insert(joint).second) return;
        archive.bytes(joint, 64);
        if (auto child = archive.pointer(joint + 8)) self(self, *child);
        if (auto next = archive.pointer(joint + 12)) self(self, *next);
        if (archive.u32(joint + 4) & ((1U << 5) | (1U << 14))) return;
        std::set<std::uint32_t> objects;
        for (auto object = archive.pointer(joint + 16); object; object = archive.pointer(*object + 4)) {
            if (!objects.insert(*object).second) throw std::runtime_error("Cyclic DObj list");
            auto material = archive.pointer(*object + 8);
            if (!material) continue;
            std::set<std::uint32_t> tex_seen;
            for (auto tex = archive.pointer(*material + 8); tex; tex = archive.pointer(*tex + 4)) {
                if (!tex_seen.insert(*tex).second) throw std::runtime_error("Cyclic TObj list");
                auto image = archive.pointer(*tex + 0x4c);
                if (image && images.insert(*image).second)
                    result.push_back({*image, archive.pointer(*tex + 0x50).value_or(UINT32_MAX)});
            }
        }
    };
    for (const auto& [name, root] : archive.roots())
        if (name.ends_with("_joint")) visit(visit, root);
    return result;
}
int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::fprintf(stderr, "Usage: melee_asset_probe <character.dat> [frame-limit]\n"); return 2;
    }
    bool initialized = false;
    try {
        auto archive = melee::AssetArchive::read(argv[1]);
        auto images = textures(archive);
        if (images.empty()) throw std::runtime_error("No joint textures found in asset");
        unsigned frame_limit = argc == 3 ? static_cast<unsigned>(std::stoul(argv[2])) : 0;
        AuroraConfig config{};
        config.appName = "Melee native asset diagnostic (not gameplay)";
#ifdef __APPLE__
    config.desiredBackend = BACKEND_METAL;
#else
    config.desiredBackend = BACKEND_VULKAN;
#endif
        config.windowWidth = 800; config.windowHeight = 600;
        config.vsync = true; config.logCallback = log_message;
        auto info = aurora_initialize(argc, argv, &config);
        initialized = true;
        GXInit(nullptr, 0);
        if (info.backend != config.desiredBackend) throw std::runtime_error("Requested native graphics backend unavailable");
        unsigned frames = 0, selected = 0, input_events = 0;
        bool exiting = false;
        while (!exiting && (!frame_limit || frames < frame_limit)) {
            for (auto event = aurora_update(); event && event->type != AURORA_NONE; ++event) {
                if (event->type == AURORA_EXIT) exiting = true;
                if (event->type == AURORA_WINDOW_RESIZED) info.windowSize = event->windowSize;
                if (event->type == AURORA_SDL_EVENT && event->sdl.type == SDL_EVENT_KEY_DOWN) {
                    ++input_events;
                    if (event->sdl.key.key == SDLK_ESCAPE) exiting = true;
                    if (event->sdl.key.key == SDLK_RIGHT) selected = (selected + 1) % images.size();
                    if (event->sdl.key.key == SDLK_LEFT) selected = (selected + images.size() - 1) % images.size();
                    std::fprintf(stderr, "Input received, texture %u/%zu\n", selected+1, images.size());
                }
            }
            if (exiting || !aurora_begin_frame()) continue;
            if (frame_limit) selected = frames % images.size();
            const auto image = images[selected].image;
            const auto width = archive.u16(image+4), height = archive.u16(image+6);
            const auto format = archive.u32(image+8);
            if (!width || !height || width > 1024 || height > 1024)
                throw std::runtime_error("Invalid GameCube texture dimensions");
            const auto data = archive.pointer(image);
            if (!data) throw std::runtime_error("Texture has no image data");
            const auto size = GXGetTexBufferSize(width, height, format, GX_FALSE, 0);
            const auto pixels = archive.bytes(*data, size);
            GXTexObj texture;
            GXTlutObj palette;
            if (format == GX_TF_C4 || format == GX_TF_C8 || format == GX_TF_C14X2) {
                const auto tlut = images[selected].palette;
                if (tlut == UINT32_MAX) throw std::runtime_error("Missing indexed texture palette");
                const auto palette_data = archive.pointer(tlut);
                if (!palette_data) throw std::runtime_error("Null texture palette");
                const auto entries = archive.u16(tlut+12);
                auto palette_bytes = archive.bytes(*palette_data, entries*2ULL);
                GXInitTlutObj(&palette, const_cast<std::byte*>(palette_bytes.data()), static_cast<GXTlutFmt>(archive.u32(tlut+4)), entries);
                GXLoadTlut(&palette, GX_TLUT0);
                GXInitTexObjCI(&texture, const_cast<std::byte*>(pixels.data()), width, height, static_cast<GXCITexFmt>(format), GX_CLAMP, GX_CLAMP, GX_FALSE, GX_TLUT0);
            } else {
                GXInitTexObj(&texture, const_cast<std::byte*>(pixels.data()), width, height, static_cast<GXTexFmt>(format), GX_CLAMP, GX_CLAMP, GX_FALSE);
            }
            GXInitTexObjLOD(&texture, GX_NEAR, GX_NEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
            GXLoadTexObj(&texture, GX_TEXMAP0);
            GXSetCopyClear({24, 27, 35, 255}, GX_MAX_Z24);
            GXSetViewport(0, 0, 640, 480, 0, 1);
            GXSetScissor(0, 0, 640, 480);
            Mtx44 projection;
            C_MTXOrtho(projection, 1, -1, -1, 1, 0, 10);
            GXSetProjection(projection, GX_ORTHOGRAPHIC);
            Mtx model; C_MTXIdentity(model); GXLoadPosMtxImm(model, GX_PNMTX0); GXSetCurrentMtx(GX_PNMTX0);
            GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
            GXSetColorUpdate(GX_TRUE); GXSetAlphaUpdate(GX_TRUE);
            GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
            GXSetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);
            GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
            GXSetCullMode(GX_CULL_NONE);
            GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
            GXSetNumChans(0); GXSetNumTexGens(1); GXSetNumTevStages(1);
            GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
            GXSetTevOp(GX_TEVSTAGE0, GX_REPLACE);
            GXSetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
            GXClearVtxDesc(); GXSetVtxDesc(GX_VA_POS, GX_DIRECT); GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
            GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
            GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
            MeleeNativeEmitQuad(); aurora_end_frame(); ++frames;
        }
        aurora_shutdown(); initialized = false;
        std::printf("PASS: Metal backend, %u frames, %zu discovered textures, %u keyboard events\n", frames, images.size(), input_events);
        return frames ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Asset diagnostic failed: %s\n", e.what());
        if (initialized) aurora_shutdown();
        return 1;
    }
}
