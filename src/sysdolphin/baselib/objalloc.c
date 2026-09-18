#include "objalloc.h"

#include <string.h>

#include "initialize.h"
#include "memory.h"
#include <dolphin/os/OSAlloc.h>

static objheap obj_heap = { 0, 0, -1, -1 };

static HSD_ObjAllocData* alloc_datas;

#ifdef MELEE_NATIVE
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>

/* MELEE_OBJALLOC_QUARANTINE=1: freed objects are poisoned and parked in a
 * ring before they rejoin their free list; a changed byte at eviction is a
 * write after free, reported with the freeing call stack as offsets from
 * HSD_ObjFree. */
#define OBJALLOC_QUARANTINE_SIZE 16384
#define OBJALLOC_QUARANTINE_FRAMES 12
#define OBJALLOC_POISON 0xA5

typedef struct ObjAllocQuarantine {
    HSD_ObjAllocData* data;
    void* obj;
    void* frames[OBJALLOC_QUARANTINE_FRAMES];
    int n_frames;
} ObjAllocQuarantine;

static ObjAllocQuarantine* objalloc_quarantine;
static u32 objalloc_quarantine_head;
static int objalloc_quarantine_enabled = -1;
static int objalloc_quarantine_reports;

static void objalloc_quarantine_clear(void)
{
    if (objalloc_quarantine != NULL) {
        memset(objalloc_quarantine, 0,
               OBJALLOC_QUARANTINE_SIZE * sizeof(*objalloc_quarantine));
    }
}

static void objalloc_quarantine_check(ObjAllocQuarantine* entry)
{
    u8* bytes = entry->obj;
    u32 size = entry->data->size;
    u32 i;
    for (i = 0; i < size; i++) {
        if (bytes[i] != OBJALLOC_POISON) {
            break;
        }
    }
    if (i < size && objalloc_quarantine_reports < 64) {
        u32 last = i;
        u32 j;
        int f;
        for (j = i; j < size; j++) {
            if (bytes[j] != OBJALLOC_POISON) {
                last = j;
            }
        }
        objalloc_quarantine_reports++;
        fprintf(stderr,
                "[objalloc-uaf] obj=%p size=%u alloc_data=%p first=%u "
                "last=%u bytes:",
                entry->obj, size, (void*) entry->data, i, last);
        for (j = i; j <= last && j < i + 24; j++) {
            fprintf(stderr, " %02x", bytes[j]);
        }
        fprintf(stderr, "\n[objalloc-uaf] freed-by (offset from HSD_ObjFree):");
        for (f = 0; f < entry->n_frames; f++) {
            fprintf(stderr, " %+ld",
                    (long) ((intptr_t) entry->frames[f] -
                            (intptr_t) &HSD_ObjFree));
        }
        fprintf(stderr, "\n");
        fflush(stderr);
    }
}

static bool objalloc_quarantine_park(HSD_ObjAllocData* data, void* obj)
{
    ObjAllocQuarantine* entry;
    if (objalloc_quarantine_enabled < 0) {
        const char* env = getenv("MELEE_OBJALLOC_QUARANTINE");
        objalloc_quarantine_enabled = env != NULL && env[0] == '1';
        if (objalloc_quarantine_enabled) {
            objalloc_quarantine = calloc(OBJALLOC_QUARANTINE_SIZE,
                                         sizeof(*objalloc_quarantine));
            objalloc_quarantine_enabled = objalloc_quarantine != NULL;
            fprintf(stderr, "[objalloc-uaf] quarantine enabled HSD_ObjFree=%p\n",
                    (void*) &HSD_ObjFree);
        }
    }
    if (!objalloc_quarantine_enabled) {
        return false;
    }
    entry = &objalloc_quarantine[objalloc_quarantine_head];
    objalloc_quarantine_head =
        (objalloc_quarantine_head + 1) % OBJALLOC_QUARANTINE_SIZE;
    if (entry->obj != NULL) {
        HSD_ObjAllocLink* link = entry->obj;
        objalloc_quarantine_check(entry);
        link->next = entry->data->freehead;
        entry->data->freehead = link;
        entry->data->free += 1;
    }
    memset(obj, OBJALLOC_POISON, data->size);
    entry->data = data;
    entry->obj = obj;
    entry->n_frames = backtrace(entry->frames, OBJALLOC_QUARANTINE_FRAMES);
    data->used -= 1;
    return true;
}
#endif

void HSD_ObjSetHeap(u32 size, void* ptr)
{
#ifdef MELEE_NATIVE
    objalloc_quarantine_clear();
#endif
    obj_heap.curr = (HSD_ObjAddress) ptr;
    obj_heap.top = (HSD_ObjAddress) ptr;
    obj_heap.remain = size;
    obj_heap.size = size;
}

s32 HSD_ObjAllocAddFree(HSD_ObjAllocData* data, u32 num)
{
    HSD_ObjAddress computed_start;
    HSD_ObjAddress pool_end;
    u32 pool_size;
    u8* pool_start;

    u8 _[4];

    HSD_ASSERT(0xEE, data);
    pool_size = data->size * num;
    if (obj_heap.top != 0) {
        pool_end = obj_heap.top + obj_heap.size;
        computed_start = (obj_heap.curr + data->align) & ~(HSD_ObjAddress) data->align;
        pool_start = (void*) computed_start;
        if (computed_start > pool_end) {
            return 0;
        }
        if (pool_end - (HSD_ObjAddress) pool_start < pool_size) {
            pool_size = pool_end - (HSD_ObjAddress) pool_start -
                        (pool_end - (HSD_ObjAddress) pool_start) % data->size;
        }
        num = pool_size / data->size;
        if (num == 0) {
            return 0;
        }
        obj_heap.curr = (HSD_ObjAddress) pool_start + pool_size;
        obj_heap.remain = pool_end - obj_heap.curr;
    } else {
        pool_start = HSD_MemAlloc(pool_size);
        if (pool_start == 0) {
            return 0;
        }
        obj_heap.remain -= pool_size;
    }

    {
        int i;
        for (i = 0; (unsigned) i < num - 1; i++) {
            *(void**) (pool_start + data->size * i) =
                (void*) (pool_start + data->size * (i + 1));
        }
        *(void**) (pool_start + data->size * i) = data->freehead;
    }

    data->freehead = (HSD_ObjAllocLink*) pool_start;
    data->free += num;
    return num;
}

void* HSD_ObjAlloc(HSD_ObjAllocData* data)
{
    HSD_ObjAllocLink* cur;
    u32 size;

    if (data->num_limit_flag && data->used >= data->num_limit) {
        return NULL;
    }
    if (data->heap_limit_flag) {
        if (data->heap_limit_num == (unsigned) -1) {
            if (obj_heap.top != 0) {
                size = obj_heap.remain;
            } else {
                size = OSCheckHeap(HSD_GetHeap());
            }
            if (size <= data->heap_limit_size) {
                data->heap_limit_num = data->used + data->free;
            }
        } else {
            if (obj_heap.top != 0) {
                size = obj_heap.remain;
            } else {
                size = OSCheckHeap(HSD_GetHeap());
            }
            if (size > data->heap_limit_size) {
                data->heap_limit_num = -1;
            }
        }
        if (data->used >= data->heap_limit_num) {
            return NULL;
        }
    }
    if (data->free == 0) {
        HSD_ObjAllocAddFree(data, 1);
        if (data->free == 0) {
            return NULL;
        }
    }
    cur = data->freehead;
    data->freehead = cur->next;
    data->used += 1;
    data->free -= 1;
    if (data->used > data->peak) {
        data->peak = data->used;
    }
    return cur;
}

void HSD_ObjFree(HSD_ObjAllocData* data, void* obj)
{
    HSD_ObjAllocLink* link = obj;
#ifdef MELEE_NATIVE
    if (objalloc_quarantine_park(data, obj)) {
        return;
    }
#endif
    link->next = data->freehead;
    data->freehead = link;
    data->free += 1;
    data->used -= 1;
}

static inline void removeAll(HSD_ObjAllocData* data)
{
    HSD_ObjAllocData** cur = &alloc_datas;
    while (*cur != NULL) {
        if (*cur == data) {
            *cur = (*cur)->next;
        } else {
            cur = &(*cur)->next;
        }
    }
}

void HSD_ObjAllocInit(HSD_ObjAllocData* data, size_t size, u32 align)
{
    HSD_ASSERT(0x185, data);
    if (data != NULL) {
        removeAll(data);
    } else {
        alloc_datas = NULL;
    }
#ifdef MELEE_NATIVE
    objalloc_quarantine_clear();
#endif
    memset(data, 0, sizeof(HSD_ObjAllocData));
    data->num_limit = -1;
    data->heap_limit_size = 0;
    data->heap_limit_num = -1;
#ifdef MELEE_NATIVE
    /* Freed objects hold host pointers, including pools of 12-byte vectors. */
    HSD_ASSERT(0, align != 0 && (align & (align - 1)) == 0);
    if (align < _Alignof(HSD_ObjAllocLink)) {
        align = _Alignof(HSD_ObjAllocLink);
    }
    if (size < sizeof(HSD_ObjAllocLink)) {
        size = sizeof(HSD_ObjAllocLink);
    }
#endif
    data->align = align - 1;
    data->size = (size + data->align) & ~(HSD_ObjAddress) data->align;
    data->next = alloc_datas;
    alloc_datas = data;
}

void _HSD_ObjAllocForgetMemory(void* low, void* high)
{
#ifdef MELEE_NATIVE
    objalloc_quarantine_clear();
#endif
    alloc_datas = NULL;
}
