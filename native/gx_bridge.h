#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Register a live native asset buffer before binding any of its vertex arrays.
 * Unregister only after its pending graphics work has finished. */
void MeleeNativeRegisterVertexBuffer(const void* data, size_t size, int little_endian);
void MeleeNativeUnregisterVertexBuffer(const void* data);
void MeleeNativeSetArrayData(int attribute, const void* data, unsigned int size,
                            unsigned char stride, int little_endian);
#ifdef __cplusplus
}
#endif
