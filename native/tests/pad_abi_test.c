#include <dolphin/pad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) abort(); } while (0)
_Static_assert(sizeof(PADStatus) == 12, "Keep the game input layout");
extern void MeleeNativeSetKeyboard(u16,s8,s8,s8,s8);
int main(void) {
    struct { unsigned char before[16]; PADStatus status[4]; unsigned char after[16]; } guarded;
    memset(&guarded, 0xA5, sizeof(guarded));
    PADSetSamplingRate(0);
    CHECK(PADRead(guarded.status) == 0xF0000000);
    for (int i = 0; i < 4; ++i) {
        CHECK(guarded.status[i].button == 0x100+i);
        CHECK(guarded.status[i].stickX == 40+i);
        CHECK(guarded.status[i].err == 0);
    }
    PADClamp(guarded.status);
    for (int i = 0; i < 4; ++i) CHECK(guarded.status[i].stickX == 10+i);
    for (int i = 0; i < 16; ++i) CHECK(guarded.before[i] == 0xA5 && guarded.after[i] == 0xA5);
    MeleeNativeSetKeyboard(PAD_BUTTON_B|PAD_TRIGGER_L,80,-80,-80,80);
    PADRead(guarded.status);
    CHECK(guarded.status[0].button==(0x100|PAD_BUTTON_B|PAD_TRIGGER_L));
    CHECK(guarded.status[0].triggerLeft==255 && guarded.status[0].stickX==80);
    CHECK(guarded.status[0].stickY==-80 && guarded.status[0].substickX==-80);
    CHECK(guarded.status[1].stickX==41);
    MeleeNativeSetKeyboard(0,0,0,0,0); PADRead(guarded.status);
    CHECK(guarded.status[0].stickX==40 && guarded.status[0].triggerLeft==0);
    puts("PASS: four extended native controller records marshal into 12-byte game records without overwriting guards");
}
