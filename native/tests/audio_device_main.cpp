#include <dolphin/ar.h>
#include <cstdio>
extern "C" int MeleeAudioDeviceTest(const char*);
extern "C" void* MeleeTestARAM();
u32 ARGetSize(void) { return 16*1024*1024; }
void* ARGetStorageAddress() { return MeleeTestARAM(); }
int main(int argc,char** argv) {
    if(argc!=2) {std::fputs("Usage: native_audio_device_test <original.ssm>\n",stderr);return 2;}
    return MeleeAudioDeviceTest(argv[1]);
}
