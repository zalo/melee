#include <dolphin/pad.h>
static_assert(sizeof(PADStatus) == 16);
extern "C" u32 PADRead(PADStatus* output) {
    for (int i = 0; i < 4; ++i) {
        output[i] = {};
        output[i].button = 0x100+i;
        output[i].stickX = 40+i;
        output[i].extButton = 0xFFFFFFFF;
    }
    return 0xF0000000;
}
extern "C" void PADClamp(PADStatus* data) {
    for (int i = 0; i < 4; ++i) data[i].stickX -= 30;
}
