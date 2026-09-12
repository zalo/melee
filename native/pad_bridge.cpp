#include <dolphin/pad.h>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace {
// Keep Melee's 12-byte input ABI separate from Aurora's 16-byte extension.
struct MeleePadStatus {
    u16 button;
    s8 stickX, stickY, substickX, substickY;
    u8 triggerLeft, triggerRight, analogA, analogB;
    s8 err;
};
static_assert(sizeof(MeleePadStatus) == 12);
static_assert(sizeof(PADStatus) == 16);
static_assert(offsetof(MeleePadStatus, err) == offsetof(PADStatus, err));
std::mutex pad_mutex;
std::chrono::milliseconds sample_period(0);
std::chrono::steady_clock::time_point last_sample;
std::array<PADStatus, 4> latest;
u32 motor_mask;
MeleePadStatus keyboard;
void copy_to_game(MeleePadStatus* game, const PADStatus* native) {
    for (unsigned i = 0; i < 4; ++i) {
        game[i] = {};
        std::memcpy(&game[i], &native[i], offsetof(MeleePadStatus, err)+1);
    }
}
#ifdef MELEE_MIYOO_FLIP
// The Flip's D-pad sits where a GameCube control stick would be used, so the
// D-pad drives the control stick and the analog stick drives the D-pad. The
// device also labels its face buttons in Nintendo order (B below A), the
// reverse of the Xbox layout its gamepad driver reports. MELEE_FLIP_SWAP_CONTROLS=0
// restores Aurora's default mapping.
bool swap_controls_enabled() {
    static const bool enabled = [] {
        const char* env = std::getenv("MELEE_FLIP_SWAP_CONTROLS");
        return env == nullptr || std::strcmp(env, "0") != 0;
    }();
    return enabled;
}
constexpr s8 kDigitalStick = 127;     // clamped to the gate by the game's PADClamp
constexpr int kStickAsDpadThreshold = 40;
void swap_flip_controls(MeleePadStatus& pad) {
    if (pad.err != PAD_ERR_NONE) return;
    constexpr u16 kDpad = PAD_BUTTON_UP | PAD_BUTTON_DOWN | PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT;
    const u16 buttons = pad.button;
    u16 out = buttons & ~(kDpad | PAD_BUTTON_A | PAD_BUTTON_B);
    if (buttons & PAD_BUTTON_A) out |= PAD_BUTTON_B;
    if (buttons & PAD_BUTTON_B) out |= PAD_BUTTON_A;
    if (pad.stickX > kStickAsDpadThreshold) out |= PAD_BUTTON_RIGHT;
    if (pad.stickX < -kStickAsDpadThreshold) out |= PAD_BUTTON_LEFT;
    if (pad.stickY > kStickAsDpadThreshold) out |= PAD_BUTTON_UP;
    if (pad.stickY < -kStickAsDpadThreshold) out |= PAD_BUTTON_DOWN;
    s8 x = 0, y = 0;
    if (buttons & PAD_BUTTON_RIGHT) x = kDigitalStick;
    if (buttons & PAD_BUTTON_LEFT) x = -kDigitalStick;
    if (buttons & PAD_BUTTON_UP) y = kDigitalStick;
    if (buttons & PAD_BUTTON_DOWN) y = -kDigitalStick;
    pad.button = out;
    pad.stickX = x;
    pad.stickY = y;
}
#endif
}
extern "C" u32 MeleeNativePADRead(MeleePadStatus* output) {
    std::lock_guard lock(pad_mutex);
    auto now = std::chrono::steady_clock::now();
    if (last_sample.time_since_epoch().count() == 0 || now-last_sample >= sample_period) {
        motor_mask = PADRead(latest.data()); last_sample = now;
    }
    copy_to_game(output, latest.data());
#ifdef MELEE_MIYOO_FLIP
    if (swap_controls_enabled())
        for (unsigned i = 0; i < 4; ++i) swap_flip_controls(output[i]);
#endif
    auto& player=output[0];
    if(player.err!=PAD_ERR_NONE) player={};
    player.button|=keyboard.button;
    if(keyboard.stickX) player.stickX=keyboard.stickX;
    if(keyboard.stickY) player.stickY=keyboard.stickY;
    if(keyboard.substickX) player.substickX=keyboard.substickX;
    if(keyboard.substickY) player.substickY=keyboard.substickY;
    if(keyboard.button&PAD_TRIGGER_L) player.triggerLeft=255;
    if(keyboard.button&PAD_TRIGGER_R) player.triggerRight=255;
    return motor_mask;
}
extern "C" void MeleeNativePADClamp(MeleePadStatus* data) {
    std::array<PADStatus, 4> native{};
    for (unsigned i = 0; i < 4; ++i)
        std::memcpy(&native[i], &data[i], offsetof(MeleePadStatus, err)+1);
    PADClamp(native.data()); copy_to_game(data, native.data());
}
extern "C" void PADSetSamplingRate(u32 milliseconds) {
    std::lock_guard lock(pad_mutex);
    sample_period = std::chrono::milliseconds(milliseconds);
}

extern "C" void MeleeNativeSetKeyboard(u16 buttons,s8 x,s8 y,s8 cx,s8 cy) {
    std::lock_guard lock(pad_mutex);
    keyboard={}; keyboard.button=buttons; keyboard.stickX=x; keyboard.stickY=y;
    keyboard.substickX=cx; keyboard.substickY=cy;
}
