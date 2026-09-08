#include <dolphin/card.h>
#include <dolphin/os.h>
#include <array>

// Aurora performs card I/O synchronously. Deliver completions at a game polling
// boundary so SDK callers can finish setting their pending state first.
namespace {
struct Completion { CARDCallback callback{}; s32 result{}; bool pending{}; };
std::array<Completion, 2> completions;
template<class Operation> s32 submit(s32 channel, CARDCallback callback, Operation operation) {
    if (channel < 0 || channel >= 2) return CARD_RESULT_FATAL_ERROR;
    const int enabled = OSDisableInterrupts();
    auto& completion = completions[channel];
    if (completion.pending) { OSRestoreInterrupts(enabled); return CARD_RESULT_BUSY; }
    const s32 result = operation();
    if (result >= 0) completion = {callback, result, true};
    OSRestoreInterrupts(enabled);
    return result;
}
}
extern "C" {
void MeleeNativePumpCards(void) {
    const int enabled = OSDisableInterrupts();
    // Snapshot first: a completion may submit the next operation on either slot.
    const auto ready = completions;
    completions = {};
    for (int channel = 0; channel < 2; ++channel)
        if (ready[channel].pending && ready[channel].callback)
            ready[channel].callback(channel, ready[channel].result);
    OSRestoreInterrupts(enabled);
}
s32 MeleeNativeCARDMountAsync(s32 channel, void* area, CARDCallback detach, CARDCallback attach) {
    return submit(channel, attach, [&] {
        s32 size, sector;
        s32 result = CARDProbeEx(channel, &size, &sector);
        return result < 0 ? result : CARDMount(channel, area, detach);
    });
}
s32 MeleeNativeCARDCheckAsync(s32 channel, CARDCallback callback) {
    return submit(channel, callback, [&] { return CARDCheck(channel); });
}
s32 MeleeNativeCARDDeleteAsync(s32 channel, const char* name, CARDCallback callback) {
    return submit(channel, callback, [&] { return CARDDelete(channel, name); });
}
s32 MeleeNativeCARDRenameAsync(s32 channel, const char* old_name, const char* new_name, CARDCallback callback) {
    return submit(channel, callback, [&] { return CARDRename(channel, old_name, new_name); });
}
s32 MeleeNativeCARDFormatAsync(s32 channel, CARDCallback callback) {
    return submit(channel, callback, [&] { return CARDFormat(channel); });
}
s32 MeleeNativeCARDCreateAsync(s32 channel, const char* name, u32 size, CARDFileInfo* file, CARDCallback callback) {
    return submit(channel, callback, [&] { return CARDCreate(channel, name, size, file); });
}
s32 MeleeNativeCARDReadAsync(CARDFileInfo* file, void* buffer, s32 size, s32 offset, CARDCallback callback) {
    if (!file) return CARD_RESULT_FATAL_ERROR;
    return submit(file->chan, callback, [&] { return CARDRead(file, buffer, size, offset); });
}
s32 MeleeNativeCARDWriteAsync(CARDFileInfo* file, const void* buffer, s32 size, s32 offset, CARDCallback callback) {
    if (!file) return CARD_RESULT_FATAL_ERROR;
    return submit(file->chan, callback, [&] { return CARDWrite(file, buffer, size, offset); });
}
s32 MeleeNativeCARDSetStatusAsync(s32 channel, s32 number, CARDStat* stat, CARDCallback callback) {
    return submit(channel, callback, [&] { return CARDSetStatus(channel, number, stat); });
}
}
