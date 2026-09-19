#include <dolphin/dvd.h>
#include <dolphin/os.h>
#include <unordered_map>
#include <vector>

extern "C" int MeleeNativeDeterministicIO(void);

namespace {
std::unordered_map<DVDFileInfo*, DVDCallback> callbacks;
void completed(s32 result, DVDFileInfo* file) {
    const auto enabled = OSDisableInterrupts();
    const auto entry = callbacks.find(file);
    if (entry == callbacks.end()) OSPanic(__FILE__, __LINE__, "DVD completion without a pending native request");
    const auto callback = entry->second;
    callbacks.erase(entry);
    if (callback) callback(result, file);
    OSRestoreInterrupts(enabled);
}
// Deterministic mode: the read runs to completion on the game thread and its callback fires at the
// next VI pump, one frame later on every device, instead of whenever Aurora's disc thread gets to
// it. Melee's loaders chain one read per callback, so a load costs one frame per file; fine.
struct Deferred { DVDFileInfo* file; DVDCallback callback; s32 result; };
std::vector<Deferred> deferred;
}
extern "C" BOOL MeleeNativeDVDReadAsyncPrio(DVDFileInfo* file, void* destination, s32 size,
                                            s32 offset, DVDCallback callback, s32 priority) {
    if (MeleeNativeDeterministicIO()) {
        const s32 result = DVDReadPrio(file, destination, size, offset, priority);
        const auto enabled = OSDisableInterrupts();
        deferred.push_back({file, callback, result});
        OSRestoreInterrupts(enabled);
        return TRUE;
    }
    const auto enabled = OSDisableInterrupts();
    if (!callbacks.emplace(file, callback).second)
        OSPanic(__FILE__, __LINE__, "DVD file already has a pending request");
    const auto result = DVDReadAsyncPrio(file, destination, size, offset, completed, priority);
    if (!result) callbacks.erase(file);
    OSRestoreInterrupts(enabled);
    return result;
}
extern "C" int MeleeNativeArqDeferredPending(void);
extern "C" int MeleeNativeDeferredIOPending(void) { return !deferred.empty() || MeleeNativeArqDeferredPending(); }
// Called from the VI loop and when the game thread re-enables interrupts (os_runtime.c);
// completions may queue further reads, so snapshot first.
extern "C" void MeleeNativePumpDvd(void) {
    if (deferred.empty()) return;
    const auto enabled = OSDisableInterrupts();
    const auto ready = std::move(deferred);
    deferred.clear();
    for (const auto& item : ready)
        if (item.callback) item.callback(item.result, item.file);
    OSRestoreInterrupts(enabled);
}
