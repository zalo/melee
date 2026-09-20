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
// netplay.cpp and keyboard_input.cpp, absent from the pad unit tests.
extern "C" int MeleeNativeNetplayActive(void) __attribute__((weak));
extern "C" int MeleeNativeNetplayPads(void* pads) __attribute__((weak));
extern "C" int MeleeNativeScriptActive(void) __attribute__((weak));
extern "C" void MeleeNativeScriptPoll(void) __attribute__((weak));
extern "C" void MeleeNativeNetplayLobbyInput(void* pads) __attribute__((weak));
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
// Optional Flip-ergonomics remap, OFF by default: the standard controller mapping now comes through
// unchanged (analog stick -> control stick, D-pad -> D-pad, A -> A, B -> B). Set
// MELEE_FLIP_SWAP_CONTROLS=1 to restore the old Miyoo-Flip layout, where the D-pad drives the control
// stick and the analog stick the D-pad (the Flip's D-pad sits where a control-stick thumb would rest),
// and A/B trade places to match the Flip's Nintendo-order face-button labels.
bool swap_controls_enabled() {
    static const bool enabled = [] {
        const char* env = std::getenv("MELEE_FLIP_SWAP_CONTROLS");
        return env != nullptr && env[0] != '\0' && std::strcmp(env, "0") != 0;
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
    // One poll = one logic frame: the scripted input (if any) steps here.
    if (MeleeNativeScriptActive && MeleeNativeScriptActive() && MeleeNativeScriptPoll) MeleeNativeScriptPoll();
    std::lock_guard lock(pad_mutex);
    auto now = std::chrono::steady_clock::now();
    if (last_sample.time_since_epoch().count() == 0 || now-last_sample >= sample_period) {
        motor_mask = PADRead(latest.data()); last_sample = now;
    }
    // MELEE_SCRIPT_PADS_ONLY=1: the physical controllers are ignored (port 0 reads as a connected,
    // centred pad; 1-3 as absent) so a scripted run gives identical input on any device.
    static const bool script_only = [] { const char* v = std::getenv("MELEE_SCRIPT_PADS_ONLY"); return v && *v && *v != '0'; }();
    if (script_only) {
        for (unsigned i = 0; i < 4; ++i) { latest[i] = {}; latest[i].err = i == 0 ? PAD_ERR_NONE : PAD_ERR_NO_CONTROLLER; }
        motor_mask = 0;
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
    // Online lobby (Z on the character select screen): reads and, while open, swallows port 0.
    if (MeleeNativeNetplayLobbyInput) MeleeNativeNetplayLobbyInput(output);
    // Online play: port 0 now holds this player's input; the session moves it to its GameCube
    // port, fills the other player's port from the network and waits for it (fixed-delay lockstep).
    if (MeleeNativeNetplayActive && MeleeNativeNetplayActive()) MeleeNativeNetplayPads(output);
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
