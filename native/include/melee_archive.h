#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int MeleeNativeArchiveCreate(void* key, const void* data, size_t size);
void* MeleeNativeArchiveAllocate(void* key, size_t size);
void* MeleeNativeArchivePublic(void* key, const char* name);
void* MeleeNativeScriptPointer(const void* field);
void MeleeNativeArchiveExtern(void* key, const char* name, void* pointer);
void MeleeNativeArchiveRelease(void* pointer);
void MeleeNativeArchiveReleaseRange(void* pointer, size_t size);
#ifdef __cplusplus
}
#endif
