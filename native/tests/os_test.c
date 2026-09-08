#include "os_runtime.h"
#include <dolphin/os.h>
#include <stdio.h>
#include <stdlib.h>
#include <fenv.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); abort(); } } while(0)
static OSTime now;
OSTime OSGetTime(void) { return now; }
u32 OSGetPhysicalMemSize(void) { return 24 * 1024 * 1024; }
static int count, order[16];
static OSAlarm once, periodic, cancel_me;
static void cancelled(OSAlarm* alarm, OSContext* context) { (void)alarm; (void)context; abort(); }
static void fired(OSAlarm* alarm, OSContext* context) {
    CHECK(context == OSGetCurrentContext());
    order[count++] = alarm == &once ? 1 : 2;
    if (alarm == &once) OSCancelAlarm(&cancel_me);
    if (count == 3) OSCancelAlarm(&periodic);
}
extern int OSJoinThread(OSThread*, void**);
static void* worker(void* argument) {
    CHECK(OSGetCurrentThread() != NULL);
    MeleeNativeReportThreadInfo();
    int enabled = OSDisableInterrupts();
    CHECK(enabled == 1);
    OSRestoreInterrupts(enabled);
    return argument;
}
int main(void) {
    MeleeNativeReportThreadInfo();
    CHECK(OSSecondsToTicks(1) == 40500000);
    int outer = OSDisableInterrupts(); CHECK(outer == 1);
    int inner = OSDisableInterrupts(); CHECK(inner == 0);
    CHECK(OSRestoreInterrupts(inner) == 0);
    CHECK(OSRestoreInterrupts(outer) == 0);
    OSInitAlarm(); OSCreateAlarm(&once); OSCreateAlarm(&periodic); OSCreateAlarm(&cancel_me);
    OSSetAlarm(&once, 10, fired); OSSetAlarm(&cancel_me, 12, cancelled);
    OSSetPeriodicAlarm(&periodic, 5, 10, fired);
    now=4; MeleeNativePumpAlarms(); CHECK(count==0);
    now=5; MeleeNativePumpAlarms(); CHECK(count==1 && order[0]==2);
    now=20; MeleeNativePumpAlarms(); CHECK(count==3 && order[1]==1 && order[2]==2);
    now=100; MeleeNativePumpAlarms(); CHECK(count==3);
    OSThread thread;
    int marker = 42;
    CHECK(OSCreateThread(&thread, worker, &marker, NULL, 0, 16, 0));
    CHECK(OSCheckActiveThreads() == 1 && thread.suspend == 1);
    CHECK(OSResumeThread(&thread) == 1);
    void* result = NULL;
    CHECK(OSJoinThread(&thread, &result) && result == &marker);
    CHECK(OSCheckActiveThreads() == 0);
    OSContext context; OSClearContext(&context); OSSetCurrentContext(&context);
    fesetround(FE_DOWNWARD); OSSaveFPUContext(&context); CHECK((context.fpscr & 3)==3);
    context.fpscr=0; OSLoadFPUContext(&context); CHECK(fegetround()==FE_TONEAREST);
    OSClearContext(&context); CHECK(OSGetCurrentContext()!=&context);
    char directory[]="/tmp/melee-os-test-XXXXXX"; CHECK(mkdtemp(directory));
    MeleeNativeConfigureOS(directory, NULL); OSSetSoundMode(0); OSSetProgressiveMode(1);
    MeleeNativeConfigureOS(directory, NULL); CHECK(OSGetSoundMode()==0 && OSGetProgressiveMode()==1);
    char path[256]; snprintf(path,sizeof(path),"%s/console-settings.txt",directory); unlink(path); rmdir(directory);
    puts("PASS: nested interrupts, alarm ordering/cancellation, native clock constants, FPU rounding, settings IO");
}
