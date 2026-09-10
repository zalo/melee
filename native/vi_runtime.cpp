#include "os_runtime.h"
#include <aurora/aurora.h>
#include <aurora/event.h>
#include <dolphin/vi.h>
#include <dolphin/os.h>
#include <imgui.h>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <algorithm>

namespace {
using Clock = std::chrono::steady_clock;
VIRetraceCallback before_retrace, after_retrace;
u32 retraces;
void* next_buffer;
void* current_buffer;
bool black = true;
bool frame_active;
bool frame_ready;
bool presented_black = true;
unsigned held_retraces;
unsigned measured_frames;
unsigned measured_game_frames;
auto measurement_start = Clock::now();
auto previous_present = Clock::now();
std::vector<double> present_intervals;
auto next_retrace = Clock::now();
constexpr auto frame_period = std::chrono::nanoseconds(16666667);
// Critical-path breakdown for the [perf] line (MELEE_FLIP_PROFILE only).
const bool breakdown = [] { const char* v = std::getenv("MELEE_FLIP_PROFILE"); return v && v[0] != '0'; }();
double sum_game_ms, sum_end_ms, sum_sleep_ms, sum_begin_ms;
auto segment_start = Clock::now(); // time VIWaitForRetrace last returned to the game
uint64_t last_drain_ns, last_fifo_ns, last_render_ns, last_pipe_ns, last_pipe_count;
double ms(Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }
}
extern "C" uint64_t aurora_fifo_drain_wait_ns(void);
extern "C" uint64_t aurora_fifo_process_ns(void);
extern "C" uint64_t aurora_render_worker_busy_ns(void);
extern "C" uint64_t aurora_pipeline_wait_ns(void);
extern "C" uint64_t aurora_pipeline_wait_count(void);
namespace {
}
extern "C" {
void MeleeNativeGameFrame(void) { ++measured_game_frames; }
void MeleeNativeCheckFrame(void);
void MeleeNativeFrameReady(void) { frame_ready = true; }
void MeleeNativePumpCards(void);
void MeleeNativeSampleKeyboard(void);
void MeleeNativeKeyboardEvent(const SDL_Event*);
u32 VIGetRetraceCount(void) { return retraces; }
u32 VIGetNextField(void) { return 0; } // Native presentation is progressive.
u32 VIGetDTVStatus(void) { return 1; }
void VISetBlack(BOOL value) { black = value != 0; }
void VISetNextFrameBuffer(void* framebuffer) { next_buffer = framebuffer; }
void* VIGetCurrentFrameBuffer(void) { return current_buffer; }
void* VIGetNextFrameBuffer(void) { return next_buffer; }
VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback callback) {
    auto previous = before_retrace; before_retrace = callback; return previous;
}
VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback callback) {
    auto previous = after_retrace; after_retrace = callback; return previous;
}
void VIWaitForRetrace(void) {
    const auto entered = Clock::now();
    if (breakdown) sum_game_ms += ms(entered - segment_start);
    // A VI tick can occur while the game waits for input or a free XFB.
    // Keep the recording frame open until an EFB copy finishes; submitting
    // an empty Aurora frame clears the displayed image and causes a flash.
    if (frame_active && (frame_ready || black != presented_black)) {
        // Composite blanking after GX drawing without mutating game GX state.
        if (black) ImGui::GetForegroundDrawList()->AddRectFilled(
            ImVec2(0,0), ImGui::GetIO().DisplaySize, IM_COL32(0,0,0,255));
        aurora_end_frame(); frame_active = false;
        if (breakdown) sum_end_ms += ms(Clock::now() - entered);
        MeleeNativeCheckFrame();
        frame_ready = false;
        presented_black = black;
        ++measured_frames;
        const auto measured_now = Clock::now();
        present_intervals.push_back(std::chrono::duration<double, std::milli>(measured_now - previous_present).count());
        previous_present = measured_now;
        const double seconds = std::chrono::duration<double>(measured_now - measurement_start).count();
        if (seconds >= 5.0) {
            std::fprintf(stderr, "[perf] presented_fps=%.2f game_render_fps=%.2f frames=%u seconds=%.3f held_retraces=%u target_hz=60\n",
                         measured_frames / seconds, measured_game_frames / seconds, measured_frames, seconds, held_retraces);
            if (breakdown) {
                // Per presented frame, game-thread wall time: outside VI (simulation + GX recording),
                // aurora_end_frame (mostly the FIFO join), the 60 Hz sleep, and begin_frame (slot wait).
                // Worker figures are busy time per presented frame on their own threads.
                const uint64_t drain = aurora_fifo_drain_wait_ns(), fifo = aurora_fifo_process_ns(), render = aurora_render_worker_busy_ns();
                const uint64_t pipeWait = aurora_pipeline_wait_ns(), pipeCount = aurora_pipeline_wait_count();
                const double n = measured_frames ? measured_frames : 1;
                std::fprintf(stderr, "[perf-breakdown] game_ms=%.3f end_ms=%.3f drain_wait_ms=%.3f sleep_ms=%.3f begin_ms=%.3f fifo_busy_ms=%.3f render_busy_ms=%.3f pipeline_wait_ms=%.3f pipeline_waits=%u\n",
                             sum_game_ms / n, sum_end_ms / n, (drain - last_drain_ns) / 1e6 / n, sum_sleep_ms / n, sum_begin_ms / n,
                             (fifo - last_fifo_ns) / 1e6 / n, (render - last_render_ns) / 1e6 / n, (pipeWait - last_pipe_ns) / 1e6 / n,
                             unsigned(pipeCount - last_pipe_count));
                last_drain_ns = drain; last_fifo_ns = fifo; last_render_ns = render; last_pipe_ns = pipeWait; last_pipe_count = pipeCount;
                sum_game_ms = sum_end_ms = sum_sleep_ms = sum_begin_ms = 0;
            }
            std::sort(present_intervals.begin(), present_intervals.end());
            const auto percentile = [&](double p) { return present_intervals[static_cast<size_t>((present_intervals.size()-1)*p)]; };
            std::fprintf(stderr, "[pacing] samples=%zu median_ms=%.3f p95_ms=%.3f p99_ms=%.3f max_ms=%.3f\n",
                         present_intervals.size(), percentile(0.5), percentile(0.95), percentile(0.99), present_intervals.back());
            present_intervals.clear();
            measured_frames = 0;
            measured_game_frames = 0;
            held_retraces = 0;
            measurement_start = measured_now;
        }
    } else if (frame_active) ++held_retraces;
    next_retrace += frame_period;
    const auto now = Clock::now();
    if (next_retrace < now) next_retrace = now;
    std::this_thread::sleep_until(next_retrace);
    const auto woke = Clock::now();
    if (breakdown) sum_sleep_ms += ms(woke - now);
    for (;;) {
        for (auto event = aurora_update(); event && event->type != AURORA_NONE; ++event) {
            if (event->type == AURORA_SDL_EVENT) MeleeNativeKeyboardEvent(&event->sdl);
            if (event->type == AURORA_EXIT) { aurora_shutdown(); std::exit(0); }
        }
        MeleeNativePumpAlarms();
        MeleeNativePumpCards();
        if (frame_active || aurora_begin_frame()) { frame_active = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (breakdown) sum_begin_ms += ms(Clock::now() - woke);
    MeleeNativeSampleKeyboard();
    ++retraces;
    const auto enabled = OSDisableInterrupts();
    if (before_retrace) before_retrace(retraces);
    current_buffer = next_buffer;
    if (after_retrace) after_retrace(retraces);
    OSRestoreInterrupts(enabled);
    segment_start = Clock::now();
}
}
