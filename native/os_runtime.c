#include "os_runtime.h"
#include <dolphin/os.h>
#include <dolphin/db.h>
#include <dolphin/base/PPCArch.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fenv.h>
#include <stdatomic.h>
#include <limits.h>
#include <execinfo.h>

static pthread_once_t lock_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t interrupt_lock;
static _Thread_local unsigned interrupt_depth;
static _Thread_local OSContext default_context;
static _Thread_local OSContext* current_context;
static _Thread_local u32 debug_msr;
static OSAlarm* alarms;
static OSErrorHandler handlers[16];
static u32 sound_mode = 1, progressive_mode = 0;
static char settings_path[PATH_MAX];
static void (*reset_callback)(int);

void OSReport(char* format, ...) {
    va_list args; va_start(args, format); vfprintf(stderr, format, args); va_end(args);
}
void OSPanic(char* file, int line, char* format, ...) {
    fprintf(stderr, "Native game panic at %s:%d: ", file, line);
    va_list args; va_start(args, format); vfprintf(stderr, format, args); va_end(args);
    fputc('\n', stderr);
    void* frames[40];
    int count=backtrace(frames,40);
    backtrace_symbols_fd(frames,count,2);
    abort();
}
static void initialize_lock(void) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&interrupt_lock, &attr);
    pthread_mutexattr_destroy(&attr);
}
int OSDisableInterrupts(void) {
    pthread_once(&lock_once, initialize_lock);
    pthread_mutex_lock(&interrupt_lock);
    return interrupt_depth++ == 0;
}
int OSRestoreInterrupts(int enabled) {
    const int was_enabled = interrupt_depth == 0;
    if (!interrupt_depth) OSPanic(__FILE__, __LINE__, "Unbalanced interrupt restore");
    --interrupt_depth;
    if (!!enabled != (interrupt_depth == 0))
        OSPanic(__FILE__, __LINE__, "Interrupt restore order mismatch");
    pthread_mutex_unlock(&interrupt_lock);
    return was_enabled;
}
void DCFlushRange(void* addr, u32 size) { (void)addr; (void)size; atomic_thread_fence(memory_order_seq_cst); }
void DCStoreRange(void* addr, u32 size) { DCFlushRange(addr, size); }
void DCInvalidateRange(void* addr, u32 size) { DCFlushRange(addr, size); }
OSContext* OSGetCurrentContext(void) { return current_context ? current_context : &default_context; }
void OSSetCurrentContext(OSContext* context) { current_context = context; }
void OSClearContext(OSContext* context) {
    memset(context, 0, sizeof(*context));
    if (current_context == context) current_context = NULL;
}
void OSSaveFPUContext(OSContext* context) {
    unsigned mode = 0;
    switch (fegetround()) {
    case FE_TOWARDZERO: mode = 1; break;
    case FE_UPWARD: mode = 2; break;
    case FE_DOWNWARD: mode = 3; break;
    }
    context->fpscr = mode;
    if (fetestexcept(FE_INVALID)) context->fpscr |= 1U << 29;
    if (fetestexcept(FE_OVERFLOW)) context->fpscr |= 1U << 28;
    if (fetestexcept(FE_UNDERFLOW)) context->fpscr |= 1U << 27;
    if (fetestexcept(FE_DIVBYZERO)) context->fpscr |= 1U << 26;
    if (fetestexcept(FE_INEXACT)) context->fpscr |= 1U << 25;
}
void OSLoadFPUContext(OSContext* context) {
    const int modes[] = {FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD};
    fesetround(modes[context->fpscr & 3]);
    feclearexcept(FE_ALL_EXCEPT);
}
u32 OSSaveContext(OSContext* context) {
    // The game's only caller captures diagnostic state; native control flow
    // uses setjmp/longjmp through the separate runtime adapter.
    memset(context, 0, sizeof(*context));
    OSSaveFPUContext(context);
    return 0;
}
u32 PPCMfmsr(void) { return debug_msr; }
void PPCMtmsr(u32 value) { debug_msr = value; }
int DBIsDebuggerPresent(void) { return 0; }
u32 OSGetConsoleSimulatedMemSize(void) { return OSGetPhysicalMemSize(); }
int OSGetResetSwitchState(void) { return 0; } // No physical console reset switch on Mac.
u32 OSGetResetCode(void) { return 0; }
OSErrorHandler OSSetErrorHandler(OSError error, OSErrorHandler handler) {
    if (error >= 16) OSPanic(__FILE__, __LINE__, "Invalid OS error number");
    int enabled = OSDisableInterrupts();
    OSErrorHandler previous = handlers[error]; handlers[error] = handler;
    OSRestoreInterrupts(enabled); return previous;
}

static void unlink_alarm(OSAlarm* alarm) {
    OSAlarm** cursor = &alarms;
    while (*cursor && *cursor != alarm) cursor = &(*cursor)->next;
    if (*cursor) *cursor = alarm->next;
    alarm->next = alarm->prev = NULL;
}
void OSInitAlarm(void) { pthread_once(&lock_once, initialize_lock); }
void OSCreateAlarm(OSAlarm* alarm) {
    int enabled = OSDisableInterrupts(); unlink_alarm(alarm);
    memset(alarm, 0, sizeof(*alarm)); OSRestoreInterrupts(enabled);
}
void OSCancelAlarm(OSAlarm* alarm) {
    int enabled = OSDisableInterrupts(); unlink_alarm(alarm);
    alarm->handler = NULL; OSRestoreInterrupts(enabled);
}
void OSSetAlarm(OSAlarm* alarm, OSTime delay, OSAlarmHandler handler) {
    int enabled = OSDisableInterrupts(); unlink_alarm(alarm);
    alarm->handler = handler; alarm->period = 0;
    alarm->fire = OSGetTime() + (delay > 0 ? delay : 0);
    alarm->next = alarms; alarms = alarm; OSRestoreInterrupts(enabled);
}
void OSSetPeriodicAlarm(OSAlarm* alarm, OSTime start, OSTime period, OSAlarmHandler handler) {
    if (period <= 0) OSPanic(__FILE__, __LINE__, "Invalid alarm period");
    int enabled = OSDisableInterrupts(); unlink_alarm(alarm);
    OSTime now = OSGetTime();
    alarm->handler = handler; alarm->start = start; alarm->period = period;
    alarm->fire = start >= now ? start : start + ((now - start) / period + 1) * period;
    alarm->next = alarms; alarms = alarm; OSRestoreInterrupts(enabled);
}
void MeleeNativePumpAlarms(void) {
    int enabled = OSDisableInterrupts();
    // Callbacks may cancel/rearm any alarm, so select again after every call.
    for (;;) {
        OSTime now = OSGetTime(); OSAlarm* due = NULL;
        for (OSAlarm* p = alarms; p; p = p->next)
            if (p->fire <= now && (!due || p->fire < due->fire)) due = p;
        if (!due) break;
        OSAlarmHandler callback = due->handler;
        if (due->period) due->fire += ((now - due->fire) / due->period + 1) * due->period;
        else { unlink_alarm(due); due->handler = NULL; }
        if (callback) callback(due, OSGetCurrentContext());
    }
    OSRestoreInterrupts(enabled);
}

static void save_settings(void) {
    if (!settings_path[0]) return;
    char temp[PATH_MAX];
    if (snprintf(temp, sizeof(temp), "%s.tmp", settings_path) >= sizeof(temp))
        OSPanic(__FILE__, __LINE__, "Settings path too long");
    FILE* file = fopen(temp, "w");
    if (!file) OSPanic(__FILE__, __LINE__, "Cannot save native settings");
    int result = fprintf(file, "MELEE_NATIVE_1 %u %u\n", sound_mode, progressive_mode);
    int closed = fclose(file);
    if (result < 0 || closed || rename(temp, settings_path))
        OSPanic(__FILE__, __LINE__, "Cannot commit native settings");
}
void MeleeNativeConfigureOS(const char* path, void (*reset)(int)) {
    reset_callback = reset;
    if (snprintf(settings_path, sizeof(settings_path), "%s/console-settings.txt", path) >= sizeof(settings_path))
        OSPanic(__FILE__, __LINE__, "Settings path too long");
    FILE* file = fopen(settings_path, "r");
    if (file) {
        unsigned sound, progressive;
        if (fscanf(file, "MELEE_NATIVE_1 %u %u", &sound, &progressive) == 2 && sound <= 1 && progressive <= 1) {
            sound_mode = sound; progressive_mode = progressive;
        }
        fclose(file);
    }
}
u32 OSGetSoundMode(void) { return sound_mode; }
u32 OSGetProgressiveMode(void) { return progressive_mode; }
void OSSetSoundMode(u32 value) { sound_mode = !!value; save_settings(); }
void OSSetProgressiveMode(u32 value) { progressive_mode = !!value; save_settings(); }
void OSResetSystem(int type, u32 code, int force_menu) {
    (void)code; (void)force_menu; save_settings();
    if (!reset_callback) OSPanic(__FILE__, __LINE__, "No native reset handler installed");
    reset_callback(type);
    OSPanic(__FILE__, __LINE__, "Native reset returned");
}

typedef struct NativeThread {
    OSThread* game;
    void* (*entry)(void*);
    void* argument;
    pthread_t host;
    int started;
    struct NativeThread* next;
} NativeThread;
static NativeThread* threads;
static _Thread_local OSThread* current_thread;
static NativeThread* find_thread(OSThread* game) {
    for (NativeThread* p = threads; p; p = p->next) if (p->game == game) return p;
    return NULL;
}
static void remove_thread(NativeThread* record) {
    NativeThread** p = &threads;
    while (*p && *p != record) p = &(*p)->next;
    if (*p) *p = record->next;
    free(record);
}
static void* run_thread(void* opaque) {
    NativeThread* record = opaque;
    current_thread = record->game;
    OSSetCurrentContext(&current_thread->context);
    int enabled = OSDisableInterrupts();
    current_thread->state = OS_THREAD_STATE_RUNNING;
    OSRestoreInterrupts(enabled);
    void* value = record->entry(record->argument);
    enabled = OSDisableInterrupts();
    current_thread->val = value;
    current_thread->state = OS_THREAD_STATE_MORIBUND;
    if (current_thread->attr & OS_THREAD_ATTR_DETACH) remove_thread(record);
    OSRestoreInterrupts(enabled);
    return value;
}
int OSCreateThread(OSThread* thread, void* (*entry)(void*), void* argument,
                   void* stack, u32 stack_size, int priority, u16 attr) {
    if (!thread || !entry || priority < 0 || priority > 31) return 0;
    int enabled = OSDisableInterrupts();
    if (find_thread(thread)) { OSRestoreInterrupts(enabled); return 0; }
    NativeThread* record = calloc(1, sizeof(*record));
    if (!record) { OSRestoreInterrupts(enabled); return 0; }
    memset(thread, 0, sizeof(*thread));
    thread->state = OS_THREAD_STATE_READY; thread->suspend = 1;
    thread->priority = thread->base = priority; thread->attr = attr;
    thread->stackBase = stack;
    thread->stackEnd = stack ? (u32*)((unsigned char*)stack - stack_size) : NULL;
    record->game = thread; record->entry = entry; record->argument = argument;
    record->next = threads; threads = record;
    OSRestoreInterrupts(enabled); return 1;
}
s32 OSResumeThread(OSThread* thread) {
    int enabled = OSDisableInterrupts();
    NativeThread* record = find_thread(thread);
    if (!record) OSPanic(__FILE__, __LINE__, "Unknown native thread");
    int previous = thread->suspend;
    if (previous > 0) --thread->suspend;
    if (!thread->suspend && !record->started) {
        record->started = 1;
        pthread_attr_t attr; pthread_attr_init(&attr);
        if (thread->attr & OS_THREAD_ATTR_DETACH)
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        int result = pthread_create(&record->host, &attr, run_thread, record);
        pthread_attr_destroy(&attr);
        if (result) OSPanic(__FILE__, __LINE__, "Native thread creation failed: %d", result);
    }
    OSRestoreInterrupts(enabled); return previous;
}
OSThread* OSGetCurrentThread(void) { return current_thread; }
int OSCheckActiveThreads(void) {
    int enabled = OSDisableInterrupts(), count = 0;
    for (NativeThread* p = threads; p; p = p->next)
        if (p->game->state != OS_THREAD_STATE_MORIBUND) ++count;
    OSRestoreInterrupts(enabled); return count;
}
int OSJoinThread(OSThread* thread, void** value) {
    int enabled = OSDisableInterrupts();
    NativeThread* record = find_thread(thread);
    if (!record || !record->started || (thread->attr & OS_THREAD_ATTR_DETACH)) {
        OSRestoreInterrupts(enabled); return 0;
    }
    pthread_t host = record->host;
    OSRestoreInterrupts(enabled);
    if (pthread_join(host, value)) return 0;
    enabled = OSDisableInterrupts(); remove_thread(record); OSRestoreInterrupts(enabled);
    return 1;
}

void MeleeNativeReportThreadInfo(void) {
    pthread_t thread = pthread_self();
#ifdef __APPLE__
    void* top = pthread_get_stackaddr_np(thread);
    size_t size = pthread_get_stacksize_np(thread);
#else
    pthread_attr_t attr;
    void* base = NULL;
    size_t size = 0;
    int result = pthread_getattr_np(thread, &attr);
    if (result) { OSReport("Cannot query native stack: %d\n", result); return; }
    result = pthread_attr_getstack(&attr, &base, &size);
    pthread_attr_destroy(&attr);
    if (result) { OSReport("Cannot read native stack: %d\n", result); return; }
    void* top = (char*) base + size;
#endif
    OSReport("Native thread stack: top=%p, size=%zu bytes\n", top, size);
}

u64 __cvt_dbl_usll(double value) {
    /* Preserve the actual CodeWarrior conversion, including its signed-limit
     * saturation, without invoking a host out-of-range floating cast. */
    u64 bits; memcpy(&bits, &value, sizeof(bits));
    unsigned exponent = (unsigned)((bits >> 52) & 0x7FF);
    if (exponent < 1023) return 0;
    if (exponent > 1085) return bits >> 63 ? 0x8000000000000000ULL : 0x7FFFFFFFFFFFFFFFULL;
    u64 significand = (bits & 0xFFFFFFFFFFFFFULL) | 0x10000000000000ULL;
    u64 magnitude = exponent >= 1075 ? significand << (exponent-1075) : significand >> (1075-exponent);
    return bits >> 63 ? 0 - magnitude : magnitude;
}
