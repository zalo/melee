#include "audio_decode.h"
static u32 address(u16 hi, u16 lo) { return ((u32)hi << 16) | lo; }
static s16 clamp16(s64 value) { return value < -32768 ? -32768 : value > 32767 ? 32767 : (s16)value; }
int MeleeNativeDecodeSample(AXPB* p, const u8* memory, size_t size, s16* sample) {
    u32 pos = address(p->addr.currentAddressHi, p->addr.currentAddressLo);
    u32 end = address(p->addr.endAddressHi, p->addr.endAddressLo);
    if (!p->state) { *sample = 0; return 1; }
    if (p->addr.format == 0) {
        if ((pos & 15) == 0) {
            if (pos / 2 >= size) return 0;
            p->adpcm.pred_scale = memory[pos / 2];
            pos += 2;
        }
        if ((pos & 15) == 1 || pos / 2 >= size) return 0;
        unsigned predictor = p->adpcm.pred_scale >> 4;
        if (predictor >= 8) return 0;
        int nibble = (memory[pos / 2] >> ((pos & 1) ? 0 : 4)) & 15;
        if (nibble >= 8) nibble -= 16;
        s64 value = (s64)nibble * (1 << (p->adpcm.pred_scale & 15)) * 2048;
        value += (s64)(s16)p->adpcm.a[predictor][0] * (s16)p->adpcm.yn1;
        value += (s64)(s16)p->adpcm.a[predictor][1] * (s16)p->adpcm.yn2;
        *sample = clamp16((value + 1024) >> 11);
        p->adpcm.yn2 = p->adpcm.yn1;
        p->adpcm.yn1 = (u16)*sample;
    } else if (p->addr.format == 10) {
        if ((u64)pos * 2 + 1 >= size) return 0;
        *sample = (s16)((memory[(size_t)pos * 2] << 8) | memory[(size_t)pos * 2 + 1]);
    } else if (p->addr.format == 25) {
        if (pos >= size) return 0;
        *sample = (s16)((s8)memory[pos] * 256);
    } else return 0;
    if (pos == end) {
        if (p->addr.loopFlag) {
            pos = address(p->addr.loopAddressHi, p->addr.loopAddressLo);
            p->adpcm.pred_scale = p->adpcmLoop.loop_pred_scale;
            p->adpcm.yn1 = p->adpcmLoop.loop_yn1;
            p->adpcm.yn2 = p->adpcmLoop.loop_yn2;
        } else p->state = 0;
    } else ++pos;
    p->addr.currentAddressHi = pos >> 16;
    p->addr.currentAddressLo = pos;
    return 1;
}
