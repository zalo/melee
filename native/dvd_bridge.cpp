#include <dolphin/dvd.h>
#include <dolphin/os.h>
#include <unordered_map>

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
}
extern "C" BOOL MeleeNativeDVDReadAsyncPrio(DVDFileInfo* file, void* destination, s32 size,
                                            s32 offset, DVDCallback callback, s32 priority) {
    const auto enabled = OSDisableInterrupts();
    if (!callbacks.emplace(file, callback).second)
        OSPanic(__FILE__, __LINE__, "DVD file already has a pending request");
    const auto result = DVDReadAsyncPrio(file, destination, size, offset, completed, priority);
    if (!result) callbacks.erase(file);
    OSRestoreInterrupts(enabled);
    return result;
}
