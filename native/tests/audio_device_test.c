#include <melee_sfx.h>
#include <dolphin/os.h>
#include <dolphin/ai.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static u8 aram[16*1024*1024];
static atomic_uint frames;
OSTime OSGetTime(void) { return 0; }
u32 OSGetPhysicalMemSize(void) { return 24*1024*1024; }
void* MeleeTestARAM(void) { return aram; }
static void tick(void) { atomic_fetch_add(&frames,1); }
int MeleeAudioDeviceTest(const char* path) {
    FILE* file=fopen(path,"rb"); if(!file) return 2;
    fseek(file,0,SEEK_END); long size=ftell(file); rewind(file);
    u8* data=malloc(size); if(fread(data,1,size,file)!=(size_t)size) abort(); fclose(file);
    u32 header=MeleeSfxRead32(data), sample_size=MeleeSfxRead32(data+4), count=MeleeSfxRead32(data+8), base=MeleeSfxRead32(data+12);
    size_t bytes=MeleeSfxGroupSize(data+16,data+32,header,count);
    AXVPB* group=malloc(bytes); void* buckets[32]={0};
    if(!MeleeSfxBuildGroup(group,bytes,data+16,data+32,header,count,base,16384,sample_size,1,buckets)) abort();
    u32 sample_offset=(header+16+31)&~31u;
    if((u64)sample_offset+sample_size>size || sample_size>sizeof(aram)-16384) abort();
    memcpy(aram+16384,data+sample_offset,sample_size);
    MeleeSfxEntry* entry=(MeleeSfxEntry*)((u8*)group+sizeof(*group));
    AIInit(NULL); AXInit(); AXRegisterCallback(tick);
    int enabled=OSDisableInterrupts();
    AXVPB* voice=AXAcquireVoice(31,NULL,0);
    AXSetVoiceAddr(voice,&entry->addr); AXSetVoiceAdpcm(voice,&entry->adpcm); AXSetVoiceAdpcmLoop(voice,&entry->loop);
    AXSetVoiceSrcType(voice,AX_SRC_TYPE_4TAP_16K); AXSetVoiceSrcRatio(voice,entry->rate/32000.0f);
    AXPBVE ve={0x2000,0}; AXPBMIX mix={.vL=0x8000,.vR=0x8000};
    AXSetVoiceVe(voice,&ve); AXSetVoiceMix(voice,&mix); AXSetVoiceState(voice,1);
    OSRestoreInterrupts(enabled);
    usleep(750000); AXQuit();
    unsigned rendered=atomic_load(&frames); free(group); free(data);
    printf("Native audio device rendered %u blocks from original sound %u\n",rendered,base);
    return rendered>=50?0:1;
}
