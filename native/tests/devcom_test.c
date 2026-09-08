#include <sysdolphin/baselib/devcom.h>
#include <sysdolphin/baselib/synth.h>
#include <dolphin/ar.h>
#include <dolphin/dvd.h>
#include <dolphin/os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) {fprintf(stderr,"DevCom test failed: %s\n",#x);abort();} } while(0)

static DVDCallback dvd_done;
static DVDFileInfo* dvd_file;
static ARQCallback aram_done;
static ARQRequest* aram_request;
static int completions, replacement_completions, expected_request;
static void* expected_argument;
static int argument;

OSTime OSGetTime(void) { return 0; }
u32 OSGetPhysicalMemSize(void) { return 24*1024*1024; }
void* HSD_AudioMalloc(size_t size) { return calloc(1,size); }
void __assert(char* file,u32 line,char* message) { OSPanic(file,line,"%s",message); }
BOOL DVDFastOpen(s32 entry,DVDFileInfo* file) { (void)entry;memset(file,0,sizeof(*file));return 1; }
BOOL DVDReadAsyncPrio(DVDFileInfo* file,void* dest,s32 size,s32 offset,DVDCallback cb,s32 priority) {
    (void)dest;(void)size;(void)offset;(void)priority;
    CHECK(!dvd_done);dvd_done=cb;dvd_file=file;return 1;
}
void ARQPostRequest(ARQRequest* request,u32 owner,u32 type,u32 priority,
                    uintptr_t source,uintptr_t dest,u32 length,ARQCallback callback) {
    (void)owner;(void)type;(void)priority;(void)source;(void)dest;(void)length;
    CHECK(!aram_done);aram_done=callback;aram_request=request;
}
static void completed(int request,intptr_t args,void* buffer,bool cancelled) {
    CHECK(request==expected_request&&args==(intptr_t)expected_argument);
    CHECK(!buffer&&cancelled);++completions;
}
static void replacement(int request,intptr_t args,void* buffer,bool cancelled) {
    completed(request,args,buffer,cancelled);++replacement_completions;
}
static void finish_disc(void) {
    CHECK(dvd_done);DVDCallback cb=dvd_done;dvd_done=NULL;cb(32,dvd_file);
}
static void finish_aram(void) {
    CHECK(aram_done);ARQCallback cb=aram_done;aram_done=NULL;cb(aram_request);
}
int main(void) {
    // Cancel in the gap between DVD completion and its final ARAM completion.
    // No replacement flags means the original callback and wide argument survive.
    expected_argument=&argument;
    expected_request=HSD_DevComRequest(1,0,32,32,0x23,1,completed,&argument);
    finish_disc();CHECK(!HSD_DevComIsBusy(1));
    HSD_DevComCancelEx(expected_request,0,NULL,NULL);
    finish_aram();CHECK(completions==1&&!dvd_done&&!aram_done);

    expected_request=HSD_DevComRequest(1,0,32,32,0x23,1,completed,NULL);
    finish_disc();HSD_DevComCancelEx(expected_request,3,replacement,&argument);
    finish_aram();CHECK(completions==2&&replacement_completions==1);

    // The same contract also holds when cancellation precedes DVD completion.
    expected_request=HSD_DevComRequest(1,0,32,32,0x23,1,completed,&argument);
    HSD_DevComCancelEx(expected_request,0,NULL,NULL);
    finish_disc();finish_aram();CHECK(completions==3);
    puts("PASS: DevCom cancellation preserves completion across DVD/ARAM timing");
}
