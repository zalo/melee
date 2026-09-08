#include <dolphin/card.h>
#include <dolphin/os.h>
#include <cassert>
#include <cstdio>

extern "C" {
void MeleeNativePumpCards();
s32 MeleeNativeCARDMountAsync(s32, void*, CARDCallback, CARDCallback);
s32 MeleeNativeCARDCheckAsync(s32, CARDCallback);
s32 MeleeNativeCARDReadAsync(CARDFileInfo*, void*, s32, s32, CARDCallback);
s32 MeleeNativeCARDDeleteAsync(s32, const char*, CARDCallback);
static unsigned depth, callbacks, operations;
static s32 backend_result;
int OSDisableInterrupts() { return depth++ == 0; }
int OSRestoreInterrupts(int enabled) { assert(depth && bool(enabled)==(depth==1)); --depth; return 0; }
s32 CARDProbeEx(s32, s32* size, s32* sector) { *size=64; *sector=8192; return backend_result; }
s32 CARDMount(s32, void*, CARDCallback) { ++operations; return backend_result; }
s32 CARDCheck(s32) { ++operations; return backend_result; }
s32 CARDDelete(s32, const char*) { ++operations; return backend_result; }
s32 CARDRename(s32, const char*, const char*) { return backend_result; }
s32 CARDFormat(s32) { return backend_result; }
s32 CARDCreate(s32, const char*, u32, CARDFileInfo*) { return backend_result; }
s32 CARDRead(const CARDFileInfo*, void* buffer, s32 size, s32 offset) {
    assert(size==4 && offset==512); ++operations;
    *static_cast<unsigned*>(buffer)=0x12345678; return backend_result;
}
s32 CARDWrite(const CARDFileInfo*, const void*, s32, s32) { return backend_result; }
s32 CARDSetStatus(s32, s32, const CARDStat*) { return backend_result; }
static void checked(s32 channel, s32 result) { assert(channel==0 && !result && depth); ++callbacks; }
static void mounted(s32 channel, s32 result) {
    checked(channel, result);
    assert(MeleeNativeCARDCheckAsync(channel, checked)==0);
}
}
int main() {
    assert(MeleeNativeCARDMountAsync(-1, nullptr, nullptr, mounted)==CARD_RESULT_FATAL_ERROR);
    assert(MeleeNativeCARDMountAsync(0, nullptr, nullptr, mounted)==0);
    assert(callbacks==0 && operations==1);
    assert(MeleeNativeCARDCheckAsync(0, checked)==CARD_RESULT_BUSY);
    assert(operations==1);
    MeleeNativePumpCards(); assert(callbacks==1 && operations==2);
    MeleeNativePumpCards(); assert(callbacks==2);
    MeleeNativePumpCards(); assert(callbacks==2);
    backend_result=CARD_RESULT_NOFILE;
    assert(MeleeNativeCARDDeleteAsync(0,"missing",checked)==CARD_RESULT_NOFILE);
    MeleeNativePumpCards(); assert(callbacks==2);
    backend_result=0;
    CARDFileInfo file{}; unsigned buffer=0;
    assert(MeleeNativeCARDReadAsync(&file,&buffer,4,512,checked)==0);
    assert(buffer==0x12345678 && callbacks==2);
    MeleeNativePumpCards(); assert(callbacks==3 && depth==0);
    assert(MeleeNativeCARDReadAsync(nullptr,&buffer,4,512,checked)==CARD_RESULT_FATAL_ERROR);
    puts("PASS: deferred card completion, chained callbacks, pending exclusion, errors, 64-bit read buffer");
}
