// Miyoo Flip control swap: the D-pad drives the control stick, the stick drives
// the D-pad, and A/B trade places. Built with MELEE_MIYOO_FLIP defined and a
// PADRead fixture that hands each port a different raw state.
#include <dolphin/pad.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAILED: %s (line %d)\n", #x, __LINE__); std::abort(); } } while (0)
struct GamePad { u16 button; s8 stickX, stickY, substickX, substickY; u8 triggerLeft, triggerRight, analogA, analogB; s8 err; };
static_assert(sizeof(GamePad) == 12);
extern "C" u32 MeleeNativePADRead(GamePad* output);
extern "C" void PADSetSamplingRate(u32);
extern "C" u32 PADRead(PADStatus* output) {
    for (int i = 0; i < 4; ++i) output[i] = {};
    output[0].button = PAD_BUTTON_A | PAD_BUTTON_UP | PAD_BUTTON_LEFT | PAD_TRIGGER_Z;  // digital only
    output[1].button = PAD_BUTTON_B | PAD_BUTTON_X;                                     // stick pushed
    output[1].stickX = 100; output[1].stickY = -100; output[1].substickX = 70;
    output[2].button = PAD_BUTTON_A | PAD_BUTTON_B;                                     // inside dead zone
    output[2].stickX = 40; output[2].stickY = -40;
    output[3].button = PAD_BUTTON_A; output[3].err = PAD_ERR_NO_CONTROLLER;             // untouched
    return 0;
}
extern "C" void PADClamp(PADStatus*) {}
int main() {
    PADSetSamplingRate(0);
    GamePad pads[4];
    MeleeNativePADRead(pads);
    // Port 0: D-pad up-left became a full stick deflection, A became B, Z kept.
    CHECK(pads[0].button == (PAD_BUTTON_B | PAD_TRIGGER_Z));
    CHECK(pads[0].stickX == -127 && pads[0].stickY == 127);
    // Port 1: stick right-down became D-pad right+down, B became A, X and the C-stick kept.
    CHECK(pads[1].button == (PAD_BUTTON_A | PAD_BUTTON_X | PAD_BUTTON_RIGHT | PAD_BUTTON_DOWN));
    CHECK(pads[1].stickX == 0 && pads[1].stickY == 0 && pads[1].substickX == 70);
    // Port 2: a stick inside the threshold presses nothing; A and B swap to the same pair.
    CHECK(pads[2].button == (PAD_BUTTON_A | PAD_BUTTON_B));
    CHECK(pads[2].stickX == 0 && pads[2].stickY == 0);
    // Port 3: a disconnected port is passed through unchanged.
    CHECK(pads[3].err == PAD_ERR_NO_CONTROLLER && pads[3].button == PAD_BUTTON_A);
    std::puts("PASS: Flip D-pad/stick and A/B swap applied after the native pad read");
    return 0;
}
