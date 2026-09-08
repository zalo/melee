#include "audio_host.h"
#include "audio_decode.h"
#include <dolphin/ai.h>
#include <dolphin/os.h>
#include <string.h>
#include <math.h>
#include "audio_coefficients.inc"

static AXVPB voices[AX_MAX_VOICES];
static unsigned char allocated[AX_MAX_VOICES];
static u64 ages[AX_MAX_VOICES], serial;
static void (*frame_callback)(void);
static void (*aux_callback[2])(void*, void*);
static void* aux_context[2];
static int initialized, ai_initialized;
static u32 dsp_rate;
static u8 stream_left, stream_right;
static u32 phase[AX_MAX_VOICES];
static s32 itd_history[AX_MAX_VOICES][32];
static unsigned itd_cursor[AX_MAX_VOICES];
static s32 delayed_sample(unsigned voice, unsigned cursor, u32 delay) {
    unsigned whole=delay>>16, fraction=delay&65535;
    s32 a=itd_history[voice][(cursor-whole)&31];
    s32 b=itd_history[voice][(cursor-whole-1)&31];
    return ((s64)a*(65536-fraction)+(s64)b*fraction)>>16;
}
static void fail(const char* text) { OSPanic(__FILE__, __LINE__, "%s", text); }
static int index_of(AXVPB* voice) {
    uintptr_t base = (uintptr_t)voices, ptr = (uintptr_t)voice;
    if (ptr < base || ptr >= base + sizeof(voices) || (ptr - base) % sizeof(AXVPB))
        fail("AX voice does not belong to the native voice pool");
    return (ptr - base) / sizeof(AXVPB);
}
static s16 clip(s64 n) { return n < -32768 ? -32768 : n > 32767 ? 32767 : n; }
static u16 volume_step(u16 volume, s16 delta) {
    int result = (int)volume + delta;
    return result < 0 ? 0 : result > 65535 ? 65535 : result;
}
/* All game callbacks and voice mutations share the native interrupt mutex. */
void MeleeNativeAudioRender(int16_t* output, unsigned count) {
    if (count != 160) fail("AX requires blocks of 160 samples");
    int enabled = OSDisableInterrupts();
    s32 buses[3][3][160] = {{{0}}};
    unsigned aram_size;
    const u8* aram = MeleeNativeARAM(&aram_size);
    if (frame_callback) frame_callback();
    for (unsigned v = 0; v < AX_MAX_VOICES; ++v) {
        AXPB* pb = &voices[v].pb;
        if (!allocated[v] || !pb->state) continue;
        if (pb->itd.shiftL>31 || pb->itd.shiftR>31 || pb->itd.targetShiftL>31 || pb->itd.targetShiftR>31)
            fail("AX interaural delay exceeds its 32-sample history");
        u32 ratio = pb->srcSelect == 2 ? 65536 : ((u32)pb->src.ratioHi << 16) | pb->src.ratioLo;
        for (unsigned n = 0; n < count; ++n) {
            s16 decoded = 0;
            if (pb->srcSelect == 2) {
                if (!MeleeNativeDecodeSample(pb, aram, aram_size, &decoded))
                    OSPanic(__FILE__,__LINE__,"Invalid AX voice %u format %u address %08x end %08x loop %08x predictor %u",v,pb->addr.format,((u32)pb->addr.currentAddressHi<<16)|pb->addr.currentAddressLo,((u32)pb->addr.endAddressHi<<16)|pb->addr.endAddressLo,((u32)pb->addr.loopAddressHi<<16)|pb->addr.loopAddressLo,pb->adpcm.pred_scale);
                memmove(pb->src.last_samples, pb->src.last_samples + 1, 3 * sizeof(u16));
                pb->src.last_samples[3] = decoded;
            } else {
                phase[v] += ratio;
                while (phase[v] >= 65536) {
                    phase[v] -= 65536;
                    if (!MeleeNativeDecodeSample(pb, aram, aram_size, &decoded))
                        OSPanic(__FILE__,__LINE__,"Invalid AX voice %u format %u address %08x end %08x loop %08x predictor %u",v,pb->addr.format,((u32)pb->addr.currentAddressHi<<16)|pb->addr.currentAddressLo,((u32)pb->addr.endAddressHi<<16)|pb->addr.endAddressLo,((u32)pb->addr.loopAddressHi<<16)|pb->addr.loopAddressLo,pb->adpcm.pred_scale);
                    memmove(pb->src.last_samples, pb->src.last_samples + 1, 3 * sizeof(u16));
                    pb->src.last_samples[3] = decoded;
                }
                if (pb->srcSelect == 0) {
                    if (pb->coefSelect > 2) fail("Invalid AX coefficient bank");
                    const s32* weights = resample_coefficients + pb->coefSelect * 512 + (phase[v] >> 9) * 4;
                    s64 filtered = 0;
                    for (unsigned tap = 0; tap < 4; ++tap)
                        filtered += (s64)(s16)pb->src.last_samples[tap] * weights[tap];
                    decoded = clip(filtered >> 15);
                } else {
                    decoded = ((s64)(s16)pb->src.last_samples[0] * (65536 - phase[v]) +
                               (s64)(s16)pb->src.last_samples[1] * phase[v]) >> 16;
                }
            }
            s32 sample = ((s32)decoded * pb->ve.currentVolume) >> 15;
            s32 spatial[3]={sample,sample,sample};
            if(pb->itd.flag) {
                unsigned cursor=itd_cursor[v];
                itd_history[v][cursor]=sample;
                // Smooth a changed delay across this 5 ms block. Fixed delays
                // preserve exact integer samples, including across block edges.
                u32 left=((s32)pb->itd.shiftL*65536)+((s32)pb->itd.targetShiftL-pb->itd.shiftL)*65536*(s64)(n+1)/count;
                u32 right=((s32)pb->itd.shiftR*65536)+((s32)pb->itd.targetShiftR-pb->itd.shiftR)*65536*(s64)(n+1)/count;
                spatial[0]=delayed_sample(v,cursor,left);
                spatial[1]=delayed_sample(v,cursor,right);
                itd_cursor[v]=(cursor+1)&31;
            }
            // Mix fields follow SDK order: LR, aux A LR, aux B LRS, S, aux A S.
            u16* mixes = (u16*)&pb->mix;
            static const unsigned bus[9] = {0,0,1,1,2,2,2,0,1};
            static const unsigned channel[9] = {0,1,0,1,0,1,2,2,2};
            for (unsigned m = 0; m < 9; ++m) {
                buses[bus[m]][channel[m]][n] += ((s64)spatial[channel[m]] * mixes[m*2]) >> 15;
                mixes[m*2] = volume_step(mixes[m*2], (s16)mixes[m*2+1]);
            }
            pb->ve.currentVolume = volume_step(pb->ve.currentVolume, pb->ve.currentDelta);

        }
        pb->src.currentAddressFrac = phase[v];
        pb->itd.shiftL=pb->itd.targetShiftL; pb->itd.shiftR=pb->itd.targetShiftR;
    }
    for (unsigned b = 0; b < 2; ++b) {
        if (!aux_callback[b]) continue;
        struct AX_AUX_DATA data = {buses[b+1][0], buses[b+1][1], buses[b+1][2]};
        aux_callback[b](&data, aux_context[b]);
        for (unsigned n = 0; n < count; ++n)
            for (unsigned ch = 0; ch < 3; ++ch) buses[0][ch][n] += buses[b+1][ch][n];
    }
    for (unsigned n = 0; n < count; ++n) {
        output[n*2] = clip(buses[0][0][n]);
        output[n*2+1] = clip(buses[0][1][n]);
    }
    OSRestoreInterrupts(enabled);
}
void AIInit(u8* stack) { (void)stack; ai_initialized = 1; dsp_rate = 0; stream_left = stream_right = 0; }
BOOL AICheckInit(void) { return ai_initialized; }
void AISetDSPSampleRate(u32 rate) { if (rate != 0) fail("Native AX currently requires 32 kHz"); dsp_rate = rate; }
u32 AIGetDSPSampleRate(void) { return dsp_rate; }
void AISetStreamVolLeft(u8 value) { stream_left = value; }
void AISetStreamVolRight(u8 value) { stream_right = value; }
u8 AIGetStreamVolLeft(void) { return stream_left; }
u8 AIGetStreamVolRight(void) { return stream_right; }
void AXInit(void) {
    int enabled = OSDisableInterrupts();
    if (initialized) { OSRestoreInterrupts(enabled); return; }
    memset(voices, 0, sizeof(voices)); memset(allocated, 0, sizeof(allocated));
    memset(phase, 0, sizeof(phase));
    initialized = 1;
    OSRestoreInterrupts(enabled);
    MeleeNativeAudioOpen(MeleeNativeAudioRender);
}
void AXQuit(void) {
    MeleeNativeAudioClose();
    int enabled = OSDisableInterrupts();
    initialized = 0; frame_callback = NULL;
    memset(aux_callback, 0, sizeof(aux_callback));
    OSRestoreInterrupts(enabled);
}
AXVPB* AXAcquireVoice(u32 priority, void (*callback)(void*), u32 context) {
    if (!priority || priority >= AX_PRIORITY_STACKS) fail("Invalid AX priority");
    int enabled = OSDisableInterrupts(), chosen = -1;
    for (unsigned i = 0; i < AX_MAX_VOICES; ++i) {
        if (!allocated[i]) { chosen = i; break; }
        if (voices[i].priority < priority && (chosen < 0 || voices[i].priority < voices[chosen].priority ||
            (voices[i].priority == voices[chosen].priority && ages[i] < ages[chosen]))) chosen = i;
    }
    AXVPB* voice = chosen < 0 ? NULL : &voices[chosen];
    if (voice) {
        if (allocated[chosen] && voice->callback) voice->callback(voice);
        memset(voice, 0, sizeof(*voice));
        voice->index = chosen; voice->priority = priority; voice->callback = callback;
        voice->userContext = context; voice->updateWrite = voice->updateData;
        allocated[chosen] = 1; ages[chosen] = ++serial;
        phase[chosen] = 0;
        memset(itd_history[chosen],0,sizeof(itd_history[chosen])); itd_cursor[chosen]=0;
    }
    OSRestoreInterrupts(enabled); return voice;
}
void AXFreeVoice(AXVPB* p) { int enabled = OSDisableInterrupts(); int i = index_of(p); allocated[i] = 0; p->pb.state = 0; OSRestoreInterrupts(enabled); }
void AXSetVoicePriority(AXVPB* p, u32 value) { if (!value || value >= 32) fail("Invalid AX priority"); int enabled = OSDisableInterrupts(); p->priority = value; ages[index_of(p)] = ++serial; OSRestoreInterrupts(enabled); }
void AXRegisterCallback(void (*callback)(void)) { int enabled = OSDisableInterrupts(); frame_callback = callback; OSRestoreInterrupts(enabled); }
void AXRegisterAuxACallback(void (*callback)(void*,void*), void* context) { int enabled = OSDisableInterrupts(); aux_callback[0] = callback; aux_context[0] = context; OSRestoreInterrupts(enabled); }
void AXRegisterAuxBCallback(void (*callback)(void*,void*), void* context) { int enabled = OSDisableInterrupts(); aux_callback[1] = callback; aux_context[1] = context; OSRestoreInterrupts(enabled); }
#define SET_STRUCT(name, type, field) void name(AXVPB* p, type* value) { int enabled = OSDisableInterrupts(); p->pb.field = *value; OSRestoreInterrupts(enabled); }
SET_STRUCT(AXSetVoiceMix, AXPBMIX, mix)
SET_STRUCT(AXSetVoiceVe, AXPBVE, ve)
SET_STRUCT(AXSetVoiceAdpcm, AXPBADPCM, adpcm)
SET_STRUCT(AXSetVoiceAdpcmLoop, AXPBADPCMLOOP, adpcmLoop)
void AXSetVoiceAddr(AXVPB* p, AXPBADDR* value) { int enabled = OSDisableInterrupts(); p->pb.addr = *value; OSRestoreInterrupts(enabled); }
void AXSetVoiceSrc(AXVPB* p, AXPBSRC* value) { int enabled = OSDisableInterrupts(); p->pb.src = *value; phase[index_of(p)] = value->currentAddressFrac; OSRestoreInterrupts(enabled); }
#define SET_SCALAR(name, type, field) void name(AXVPB* p, type value) { int enabled = OSDisableInterrupts(); p->pb.field = value; OSRestoreInterrupts(enabled); }
SET_SCALAR(AXSetVoiceVeDelta, s16, ve.currentDelta)
SET_SCALAR(AXSetVoiceLoop, u16, addr.loopFlag)
void AXSetVoiceState(AXVPB* p, u16 state) { int enabled = OSDisableInterrupts(); p->pb.state = state; OSRestoreInterrupts(enabled); }
#define SET_ADDRESS(name, field) void name(AXVPB* p, u32 value) { int enabled = OSDisableInterrupts(); p->pb.addr.field##Hi = value >> 16; p->pb.addr.field##Lo = value; OSRestoreInterrupts(enabled); }
SET_ADDRESS(AXSetVoiceCurrentAddr, currentAddress)
SET_ADDRESS(AXSetVoiceEndAddr, endAddress)
SET_ADDRESS(AXSetVoiceLoopAddr, loopAddress)
void AXSetVoiceSrcRatio(AXVPB* p, float ratio) {
    if (!isfinite(ratio) || ratio < 0) fail("Invalid AX sample rate ratio");
    u32 value = ratio >= 4 ? 0x40000 : (u32)(ratio * 65536);
    int enabled = OSDisableInterrupts(); p->pb.src.ratioHi = value >> 16; p->pb.src.ratioLo = value; OSRestoreInterrupts(enabled);
}
void AXSetVoiceSrcType(AXVPB* p, u32 type) {
    if (type > AX_SRC_TYPE_4TAP_16K) fail("Invalid AX source type");
    int enabled = OSDisableInterrupts(); p->pb.srcSelect = type == 0 ? 2 : type == 1 ? 1 : 0;
    p->pb.coefSelect = type >= 2 ? type - 2 : 0; OSRestoreInterrupts(enabled);
}
void AXSetVoiceItdOn(AXVPB* p) { int enabled = OSDisableInterrupts(); p->pb.itd.flag = 1; p->pb.itd.shiftL=p->pb.itd.shiftR=p->pb.itd.targetShiftL=p->pb.itd.targetShiftR=0; memset(itd_history[index_of(p)],0,sizeof(itd_history[0])); itd_cursor[index_of(p)]=0; OSRestoreInterrupts(enabled); }
void AXSetVoiceItdTarget(AXVPB* p, u16 left, u16 right) { int enabled = OSDisableInterrupts(); p->pb.itd.targetShiftL = left; p->pb.itd.targetShiftR = right; OSRestoreInterrupts(enabled); }
