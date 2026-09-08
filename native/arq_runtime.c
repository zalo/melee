#include "audio_host.h"
#include <dolphin/ar.h>
#include <dolphin/os.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

typedef struct Transfer {
    struct Transfer* next;
    ARQRequest* request;
    u32 owner, type, priority, length;
    uintptr_t source, dest;
    ARQCallback callback;
} Transfer;
static Transfer* head;
static Transfer* tail;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready = PTHREAD_COND_INITIALIZER;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static u32 chunk_size = 4096;
static void* worker(void* unused) {
    (void)unused;
    for (;;) {
        pthread_mutex_lock(&mutex);
        while (!head) pthread_cond_wait(&ready, &mutex);
        pthread_mutex_unlock(&mutex);
        // A DMA completion is an interrupt: it cannot reenter game code until
        // the caller has finished its critical section and restored interrupts.
        int enabled = OSDisableInterrupts();
        pthread_mutex_lock(&mutex);
        Transfer* transfer = head;
        if (transfer) { head = transfer->next; if (!head) tail = NULL; }
        pthread_mutex_unlock(&mutex);
        if (transfer) {
            unsigned size;
            u8* aram = MeleeNativeARAM(&size);
            uintptr_t offset = transfer->type == 0 ? transfer->dest : transfer->source;
            void* memory = (void*)(transfer->type == 0 ? transfer->source : transfer->dest);
            if (!aram || !memory || offset > size || transfer->length > size - offset)
                OSPanic(__FILE__, __LINE__, "ARQ transfer outside ARAM or null host buffer");
            if (transfer->type == 0) memcpy(aram + offset, memory, transfer->length);
            else memcpy(memory, aram + offset, transfer->length);
            if (transfer->callback) transfer->callback(transfer->request);
            free(transfer);
        }
        OSRestoreInterrupts(enabled);
    }
    return NULL;
}
static void start_worker(void) {
    pthread_t thread;
    if (pthread_create(&thread, NULL, worker, NULL)) OSPanic(__FILE__, __LINE__, "Cannot create ARQ worker");
    pthread_detach(thread);
}
void ARQInit(void) { pthread_once(&once, start_worker); }
void ARQPostRequest(ARQRequest* request, u32 owner, u32 type, u32 priority,
                    uintptr_t source, uintptr_t dest, u32 length, ARQCallback callback) {
    if (!request || type > 1 || priority > 1) OSPanic(__FILE__, __LINE__, "Invalid ARQ request");
    ARQInit();
    Transfer* transfer = malloc(sizeof(*transfer));
    if (!transfer) OSPanic(__FILE__, __LINE__, "Cannot allocate ARQ request");
    *transfer = (Transfer){NULL, request, owner, type, priority, length, source, dest, callback};
    pthread_mutex_lock(&mutex);
    // Preserve ordering within a priority, especially paired relay-buffer copies.
    if (priority && head && !head->priority) { transfer->next = head; head = transfer; }
    else if (priority && head) {
        Transfer* cursor = head;
        while (cursor->next && cursor->next->priority) cursor = cursor->next;
        transfer->next = cursor->next; cursor->next = transfer;
        if (!transfer->next) tail = transfer;
    } else { if (tail) tail->next = transfer; else head = transfer; tail = transfer; }
    pthread_cond_signal(&ready);
    pthread_mutex_unlock(&mutex);
}
static void remove_matching(ARQRequest* request, u32 owner, int mode) {
    int enabled = OSDisableInterrupts();
    pthread_mutex_lock(&mutex);
    Transfer** link = &head; tail = NULL;
    while (*link) {
        Transfer* t = *link;
        if (mode == 2 || (mode == 0 ? t->request == request : t->owner == owner)) { *link = t->next; free(t); }
        else { tail = t; link = &t->next; }
    }
    pthread_mutex_unlock(&mutex); OSRestoreInterrupts(enabled);
}
void ARQRemoveRequest(ARQRequest* request) { remove_matching(request, 0, 0); }
void ARQRemoveOwnerRequest(u32 owner) { remove_matching(NULL, owner, 1); }
void ARQFlushQueue(void) { remove_matching(NULL, 0, 2); }
void ARQReset(void) { ARQFlushQueue(); }
void ARQSetChunkSize(u32 size) { if (!size || (size & 31)) OSPanic(__FILE__, __LINE__, "Invalid ARQ chunk size"); chunk_size = size; }
u32 ARQGetChunkSize(void) { return chunk_size; }
