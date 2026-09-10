#include "webgpu/gpu.hpp"
#include "gfx/render_worker.hpp"
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>
#include <aurora/gfx.h>
extern "C" void MeleeNativeTraceCamera(void);

static int current_scene = -1;
static std::atomic<bool> pending;
static std::atomic<bool> captured_once;
extern "C" void MeleeNativeRenderCheckScene(int scene) { current_scene = scene; }

// Numeric coverage by default; an explicit path enables a diagnostic PPM.
extern "C" void MeleeNativeCheckFrame() {
    if ((!std::getenv("MELEE_MATRIX_TEST") && !std::getenv("MELEE_RENDER_CHECK")) || current_scene != 2) return;
    static const bool once = [] {
        const char* value = std::getenv("MELEE_FLIP_CAPTURE_ONCE");
        return value && std::atoi(value) != 0;
    }();
    if (once && captured_once.load(std::memory_order_relaxed)) return;
    static unsigned frames;
    static const unsigned interval = [] {
        const char* text = std::getenv("MELEE_CAPTURE_INTERVAL");
        unsigned n = text ? std::strtoul(text, nullptr, 10) : 120;
        return n >= 1 && n <= 36000 ? n : 120;
    }();
    if (++frames % interval || pending.exchange(true)) return;
    MeleeNativeTraceCamera();
    const auto* stats = aurora_get_stats();
    std::fprintf(stderr, "[gpu-check] draws=%u vertices_bytes=%u\n", stats->drawCallCount, stats->lastVertSize);
    aurora::gfx::render_worker::enqueue_work([] {
        using namespace aurora::webgpu;
        const auto& source = present_source();
        if (source.format != wgpu::TextureFormat::RGBA8Unorm &&
            source.format != wgpu::TextureFormat::BGRA8Unorm) {
            std::fprintf(stderr, "[render-check] unsupported-format=%u\n", unsigned(source.format));
            pending = false; return;
        }
        const char* capture = std::getenv("MELEE_CAPTURE_FRAME");
        unsigned width = capture ? source.size.width : source.size.width * 3 / 4;
        unsigned height = capture ? source.size.height : source.size.height / 2;
        unsigned pitch = (width * 4 + 255) & ~255U;
        const uint64_t size = uint64_t(pitch) * height;
        wgpu::BufferDescriptor desc{};
        desc.size = size;
        desc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
        auto buffer = g_device.CreateBuffer(&desc);
        auto encoder = g_device.CreateCommandEncoder();
        wgpu::TexelCopyTextureInfo input{};
        input.texture = source.texture;
        input.origin = capture ? wgpu::Origin3D{0, 0, 0}
                               : wgpu::Origin3D{source.size.width / 8, source.size.height / 4, 0};
        wgpu::TexelCopyBufferInfo output{};
        output.buffer = buffer;
        output.layout.bytesPerRow = pitch;
        output.layout.rowsPerImage = height;
        wgpu::Extent3D extent{width, height, 1};
        encoder.CopyTextureToBuffer(&input, &output, &extent);
        auto commands = encoder.Finish();
        g_queue.Submit(1, &commands);
        buffer.MapAsync(wgpu::MapMode::Read, 0, size, wgpu::CallbackMode::AllowSpontaneous,
            [buffer, size, width, height, pitch, capture, format = source.format](wgpu::MapAsyncStatus status, wgpu::StringView) {
                if (status == wgpu::MapAsyncStatus::Success) {
                    auto data = static_cast<const unsigned char*>(buffer.GetConstMappedRange(0, size));
                    if (capture) {
                        const std::string partial = std::string(capture) + ".partial";
                        if (auto* file = std::fopen(partial.c_str(), "wb")) {
                            std::fprintf(file, "P6\n%u %u\n255\n", width, height);
                            std::vector<unsigned char> row(width * 3);
                            for (unsigned y = 0; y < height; ++y) {
                                for (unsigned x = 0; x < width; ++x) {
                                    const auto* p = data + y * pitch + x * 4;
                                    const bool bgra = format == wgpu::TextureFormat::BGRA8Unorm;
                                    row[x * 3] = p[bgra ? 2 : 0];
                                    row[x * 3 + 1] = p[1];
                                    row[x * 3 + 2] = p[bgra ? 0 : 2];
                                }
                                std::fwrite(row.data(), 1, row.size(), file);
                            }
                            const bool ok = !std::ferror(file);
                            if (std::fclose(file) == 0 && ok) std::rename(partial.c_str(), capture);
                        }
                    }
                    unsigned lit = 0, samples = 0, edges = 0, pairs = 0;
                    std::array<unsigned, 4096> histogram{};
                    for (unsigned y = 0; y < height; y += 8)
                        for (unsigned x = 0; x < width; x += 8) {
                            const auto* p = data + y * pitch + x * 4;
                            lit += std::max({p[0], p[1], p[2]}) > 16;
                            ++histogram[((p[0] >> 4) << 8) | ((p[1] >> 4) << 4) | (p[2] >> 4)];
                            if (x >= 8) {
                                const auto* previous = p - 32;
                                edges += std::max({std::abs(int(p[0]) - previous[0]),
                                                   std::abs(int(p[1]) - previous[1]),
                                                   std::abs(int(p[2]) - previous[2])}) > 12;
                                ++pairs;
                            }
                            ++samples;
                        }
                    std::fprintf(stderr, "[render-check] center_nonblack=%.6f samples=%u\n", double(lit) / samples, samples);
                    const auto colors = std::count_if(histogram.begin(), histogram.end(), [](unsigned n) { return n != 0; });
                    const double dominant = double(*std::max_element(histogram.begin(), histogram.end())) / samples;
                    const double edge_fraction = pairs ? double(edges) / pairs : 0;
                    std::fprintf(stderr, "[render-detail] colors=%zu dominant=%.6f edges=%.6f\n", size_t(colors), dominant, edge_fraction);
                    buffer.Unmap();
                    captured_once.store(true, std::memory_order_relaxed);
                } else std::fprintf(stderr, "[render-check] map-failed\n");
                pending = false;
            });
    });
}
