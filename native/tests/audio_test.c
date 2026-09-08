#include "audio_decode.h"
#include "audio_host.h"
#include <dolphin/os.h>
#include <dolphin/ai.h>
#include <dolphin/axfx.h>
#include <dolphin/ar.h>
#include <stdatomic.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); abort(); } } while(0)
static u8 memory[1024];
static void (*render)(int16_t*, unsigned);
void MeleeNativeAudioOpen(void (*callback)(int16_t*, unsigned)) { render = callback; }
void MeleeNativeAudioClose(void) { render = NULL; }
void* MeleeNativeARAM(unsigned* size) { *size = sizeof(memory); return memory; }
OSTime OSGetTime(void) { return 0; }
u32 OSGetPhysicalMemSize(void) { return 24*1024*1024; }
static atomic_int transfer_done;
static ARQRequest transfer_request;
static void transfer_complete(ARQRequest* request) { CHECK(request == &transfer_request); atomic_store(&transfer_done, 1); }
static int stolen, frames;
static void steal(void* voice) { CHECK(((AXVPB*)voice)->userContext == 42); ++stolen; }
static void tick(void) { ++frames; }
int main(void) {
    AXPB pb = {0}; s16 sample;
    memory[0] = 0x7f; memory[1] = 0xff; memory[2] = 0x80; memory[3] = 0;
    pb.state = 1; pb.addr.format = 10; pb.addr.endAddressLo = 1;
    CHECK(MeleeNativeDecodeSample(&pb, memory, 4, &sample) && sample == 32767 && pb.state);
    CHECK(MeleeNativeDecodeSample(&pb, memory, 4, &sample) && sample == -32768 && !pb.state);
    pb.state = 1; pb.addr.currentAddressLo = 2; CHECK(!MeleeNativeDecodeSample(&pb, memory, 4, &sample));
    memset(&pb, 0, sizeof(pb)); pb.state = 1; pb.addr.format = 25; pb.addr.endAddressLo = 1;
    CHECK(MeleeNativeDecodeSample(&pb, memory, 4, &sample) && sample == 32512);
    CHECK(MeleeNativeDecodeSample(&pb, memory, 4, &sample) && sample == -256);
    memset(&pb, 0, sizeof(pb)); pb.state = 1; pb.addr.currentAddressLo = 2;
    pb.addr.endAddressLo = 3; pb.addr.loopFlag = 1; pb.addr.loopAddressLo = 2;
    memory[1] = 0x78; pb.adpcm.pred_scale = 1; pb.adpcmLoop.loop_pred_scale = 2;
    CHECK(MeleeNativeDecodeSample(&pb, memory, 2, &sample) && sample == 14);
    CHECK(MeleeNativeDecodeSample(&pb, memory, 2, &sample) && sample == -16);
    CHECK(pb.addr.currentAddressLo == 2 && pb.adpcm.pred_scale == 2);
    CHECK(MeleeNativeDecodeSample(&pb, memory, 2, &sample) && sample == 28);
    // A streaming loop can jump beyond the old end address. The DSP compares
    // equality; the game updates the end address at the following callback.
    pb.addr.loopAddressLo=18; pb.addr.currentAddressLo=3; memory[9]=0x12;
    CHECK(MeleeNativeDecodeSample(&pb,memory,sizeof(memory),&sample));
    CHECK(pb.addr.currentAddressLo==18);
    CHECK(MeleeNativeDecodeSample(&pb,memory,sizeof(memory),&sample));
    CHECK(pb.addr.currentAddressLo==19);
    // Predictor history, saturation, and a header crossing.
    pb.addr.loopFlag = 0; pb.addr.endAddressLo = 18; pb.addr.currentAddressLo = 15;
    pb.adpcm.a[0][0] = 2048; pb.adpcm.yn1 = 32760; pb.adpcm.pred_scale = 4;
    memory[7] = 7; memory[8] = 0; memory[9] = 0x10;
    CHECK(MeleeNativeDecodeSample(&pb, memory, sizeof(memory), &sample) && sample == 32767);
    CHECK(MeleeNativeDecodeSample(&pb, memory, sizeof(memory), &sample) && sample == 32767);
    CHECK(pb.addr.currentAddressLo == 18 && !pb.state);
    AIInit(NULL); CHECK(AICheckInit()); AXInit(); AXRegisterCallback(tick);
    AXVPB* first = AXAcquireVoice(1, steal, 42); CHECK(first);
    for (int i = 1; i < AX_MAX_VOICES; ++i) CHECK(AXAcquireVoice(1, NULL, 0));
    CHECK(!AXAcquireVoice(1, NULL, 0));
    CHECK(AXAcquireVoice(2, NULL, 0) == first && stolen == 1);
    AXSetVoiceSrcType(first, AX_SRC_TYPE_NONE);
    memory[0] = 0x40; memory[1] = 0;
    AXPBADDR addr = {.loopFlag=1, .format=10}; AXSetVoiceAddr(first, &addr);
    AXPBVE ve = {.currentVolume=0x8000}; AXSetVoiceVe(first, &ve);
    AXPBMIX mix = {.vL=0x8000, .vR=0x4000}; AXSetVoiceMix(first, &mix); AXSetVoiceState(first, 1);
    s16 output[320]; render(output, 160); CHECK(frames == 1);
    for (int i = 0; i < 160; ++i) CHECK(output[i*2] == 16384 && output[i*2+1] == 8192);
    // Generated filters preserve a constant signal through all three banks
    // at half, normal, and double playback rate after history settles.
    for (unsigned bank = 0; bank < 3; ++bank) {
        AXSetVoiceSrcType(first, 2 + bank);
        for (unsigned ratio = 32768; ratio <= 131072; ratio *= 2) {
            first->pb.src.ratioHi = ratio >> 16;
            first->pb.src.ratioLo = ratio & 0xffff;
            render(output, 160);
            render(output, 160);
            for (int i = 0; i < 160; ++i)
                CHECK(output[i*2] == 16384 && output[i*2+1] == 8192);
        }
    }
    AXSetVoiceSrcType(first, AX_SRC_TYPE_NONE);
    AXSetVoiceItdOn(first); AXSetVoiceItdTarget(first,0,7);
    first->pb.itd.shiftR=7;
    render(output,160);
    for(int i=0;i<160;++i) CHECK(output[i*2]==16384 && output[i*2+1]==(i<7?0:8192));
    render(output,160);
    for(int i=0;i<160;++i) CHECK(output[i*2]==16384 && output[i*2+1]==8192);
    AXSetVoiceItdTarget(first,31,0); render(output,160);
    CHECK(first->pb.itd.shiftL==31 && first->pb.itd.shiftR==0);
    AXFreeVoice(first); render(output, 160);
    for (int i = 0; i < 320; ++i) CHECK(output[i] == 0);
    AXQuit(); CHECK(!render);
    u8 dma_source[32]; memset(dma_source, 0xab, sizeof(dma_source));
    int enabled = OSDisableInterrupts();
    ARQPostRequest(&transfer_request, 7, 0, 1, (uintptr_t)dma_source, 128, sizeof(dma_source), transfer_complete);
    usleep(10000); CHECK(!atomic_load(&transfer_done));
    OSRestoreInterrupts(enabled);
    for (unsigned i = 0; i < 1000 && !atomic_load(&transfer_done); ++i) usleep(1000);
    CHECK(atomic_load(&transfer_done) && !memcmp(memory + 128, dma_source, 32));
    // Delay impulse emerges exactly one 160-sample block later at 10 ms setting.
    struct AXFX_DELAY delay = {.delay={10,10,10}, .feedback={0,0,0}, .output={100,100,100}};
    CHECK(AXFXDelayInit(&delay));
    s32 left[160]={16384}, right[160]={0}, surround[160]={0};
    struct AXFX_BUFFERUPDATE update = {left,right,surround};
    AXFXDelayCallback(&update, &delay); CHECK(left[0] == 0);
    AXFXDelayCallback(&update, &delay); CHECK(left[0] == 16384);
    AXFXDelayShutdown(&delay); AXFXDelayShutdown(&delay);
    struct AXFX_REVERBSTD reverb = {.coloration=.5f, .mix=1, .time=1.88f, .damping=.5f};
    CHECK(AXFXReverbStdInit(&reverb));
    long long energy = 0;
    for (unsigned block = 0; block < 100; ++block) {
        memset(left, 0, sizeof(left)); if (!block) left[0]=16384;
        AXFXReverbStdCallback(&update, &reverb);
        for (unsigned n=0; n<160; ++n) { energy += abs(left[n]); CHECK(right[n]==0 && surround[n]==0); }
    }
    CHECK(energy > 16384); AXFXReverbStdShutdown(&reverb);
    AXFXReverbStdShutdown(&reverb);
    struct AXFX_REVERBHI high={.coloration=.5f,.mix=1,.time=1.5f,.damping=.5f,.crosstalk=.5f};
    CHECK(AXFXReverbHiInit(&high)); long long right_energy=0;energy=0;
    for(unsigned block=0;block<100;++block) {
        memset(left,0,sizeof(left));memset(right,0,sizeof(right));memset(surround,0,sizeof(surround));
        if(!block)left[0]=16384;
        AXFXReverbHiCallback(&update,&high);
        for(unsigned n=0;n<160;++n) {energy+=abs(left[n]);right_energy+=abs(right[n]);CHECK(surround[n]==0);}
    }
    CHECK(energy && right_energy);AXFXReverbHiShutdown(&high);AXFXReverbHiShutdown(&high);
    struct AXFX_CHORUS chorus={.baseDelay=10,.variation=1,.period=500};
    CHECK(AXFXChorusInit(&chorus));energy=0;
    for(unsigned block=0;block<10;++block) {
        memset(left,0,sizeof(left));memset(right,0,sizeof(right));memset(surround,0,sizeof(surround));
        if(!block)left[0]=16384;
        AXFXChorusCallback(&update,&chorus);
        for(unsigned n=0;n<160;++n) {energy+=abs(left[n]);CHECK(right[n]==0 && surround[n]==0);}
    }
    CHECK(energy);AXFXChorusShutdown(&chorus);AXFXChorusShutdown(&chorus);
    puts("PASS: PCM8/16, ADPCM history/loop/header/clipping/bounds, voice stealing, stereo mix and callback");
}
