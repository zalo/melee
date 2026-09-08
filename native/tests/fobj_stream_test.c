#include <sysdolphin/baselib/fobj.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define assert(value) do { if (!(value)) { fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #value); abort(); } } while (0)

void HSD_ObjAllocInit(HSD_ObjAllocData* pool, size_t size, u32 align) { pool->size = size; }
void* HSD_ObjAlloc(HSD_ObjAllocData* pool) { return calloc(1, pool->size); }
void HSD_ObjFree(HSD_ObjAllocData* pool, void* value) { free(value); }
void __assert(char* file, u32 line, char* condition) { fprintf(stderr, "%s:%u: %s\n", file, line, condition); abort(); }
// This lifetime test uses a constant track, not spline interpolation.
f32 splGetHelmite(f32 a, f32 b, f32 c, f32 d, f32 e, f32 f) { abort(); }
static void update(void* object, u32 type, HSD_ObjData* value) { *(float*)object = value->fv; }
int main(void) {
    HSD_FObjInitAllocData();
    const u8 encoded[] = {0x11, 0, 0, 128, 63, 5, 0, 0, 128, 63, 5}; // Two constant 1.0 keyframes.
    u8* scratch = malloc(sizeof(encoded));
    memcpy(scratch, encoded, sizeof(encoded));
    HSD_FObjDesc desc = {0};
    desc.ad = scratch; desc.length = sizeof(encoded); desc.type = 5;
    HSD_FObj* track = HSD_FObjLoadDesc(&desc);
    assert(track->ad_head != scratch);
    memset(scratch, 0xDD, sizeof(encoded));
    free(scratch);
    assert(memcmp(track->ad_head, encoded, sizeof(encoded)) == 0);
    float value = -1;
    HSD_FObjReqAnimAll(track, 0);
    for (unsigned i = 0; i < 6; ++i)
        HSD_FObjInterpretAnim(track, &value, update, 1);
    assert(value == 1);
    HSD_FObjRemove(track);
    puts("PASS: active animation survives source-buffer replacement");
}
