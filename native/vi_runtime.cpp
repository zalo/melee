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
auto next_retrace = Clock::now();
constexpr auto frame_period = std::chrono::nanoseconds(16666667);
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
    // A VI tick can occur while the game waits for input or a free XFB.
    // Keep the recording frame open until an EFB copy finishes; submitting
    // an empty Aurora frame clears the displayed image and causes a flash.
    if (frame_active && (frame_ready || black != presented_black)) {
        // Composite blanking after GX drawing without mutating game GX state.
        if (black) ImGui::GetForegroundDrawList()->AddRectFilled(
            ImVec2(0,0), ImGui::GetIO().DisplaySize, IM_COL32(0,0,0,255));
        aurora_end_frame(); frame_active = false;
        MeleeNativeCheckFrame();
        frame_ready = false;
        presented_black = black;
        ++measured_frames;
        const auto measured_now = Clock::now();
        const double seconds = std::chrono::duration<double>(measured_now - measurement_start).count();
        if (seconds >= 5.0) {
            std::fprintf(stderr, "[perf] presented_fps=%.2f game_render_fps=%.2f frames=%u seconds=%.3f held_retraces=%u target_hz=60\n",
                         measured_frames / seconds, measured_game_frames / seconds, measured_frames, seconds, held_retraces);
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
    MeleeNativeSampleKeyboard();
    ++retraces;
    const auto enabled = OSDisableInterrupts();
    if (before_retrace) before_retrace(retraces);
    current_buffer = next_buffer;
    if (after_retrace) after_retrace(retraces);
    OSRestoreInterrupts(enabled);
}
}
