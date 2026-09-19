#include "random.h"

static u32 seed = 1;
u32* HSD_RandSeedPtr = &seed;

#ifdef MELEE_NATIVE
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* The AX frame callback runs on the audio thread at the audio clock's pace, so random numbers it
 * draws (sound-effect panning and the like) would advance the game's seed at wall-clock times and
 * make two devices' simulations drift apart. Off the game thread the generator uses its own seed;
 * the game's sequence stays a pure function of the frames it has simulated. */
int MeleeNativeOnGameThread(void) __attribute__((weak));
extern unsigned melee_native_logic_frames __attribute__((weak));
static u32 off_thread_seed = 0x2545F491;
static u32* seed_ptr(void)
{
    if (MeleeNativeOnGameThread && !MeleeNativeOnGameThread()) {
        return &off_thread_seed;
    }
    return HSD_RandSeedPtr;
}
/* MELEE_TRACE_RAND=lo-hi: every draw from the game seed during logic frames lo..hi prints its
 * call stack (raw addresses; symbolize against the unstripped binary with the program base that
 * is printed first). For finding what consumes random numbers on one device and not the other. */
static void trace_draw(u32* p)
{
    static int state = -1; /* -1 unread, 0 off, 1 on */
    static unsigned lo, hi, count;
    if (state < 0) {
        const char* v = getenv("MELEE_TRACE_RAND");
        state = 0;
        if (v && sscanf(v, "%u-%u", &lo, &hi) == 2) {
            state = 1;
            /* The executable's load base: the maps line of /proc/self/exe's first mapping. */
            FILE* maps = fopen("/proc/self/maps", "r");
            char line[512];
            while (maps && fgets(line, sizeof line, maps)) {
                if (strstr(line, "melee") && strstr(line, "r--p") || strstr(line, "melee") && strstr(line, "r-xp")) {
                    fprintf(stderr, "[rand-trace] maps: %s", line);
                    break;
                }
            }
            if (maps) fclose(maps);
        }
    }
    if (state != 1 || p != HSD_RandSeedPtr || !&melee_native_logic_frames) return;
    unsigned frame = melee_native_logic_frames;
    if (frame < lo || frame > hi) return;
    void* frames[10];
    int n = backtrace(frames, 10);
    fprintf(stderr, "[rand-trace] frame=%u draw=%u seed=%u stack=", frame, ++count, *p);
    for (int i = 1; i < n; i++) fprintf(stderr, "%p ", frames[i]);
    fprintf(stderr, "\n");
}
#else
#define seed_ptr() HSD_RandSeedPtr
#define trace_draw(p) ((void) 0)
#endif

s32 HSD_Rand(void)
{
    u32* p = seed_ptr();
    trace_draw(p);
    *p = *p * 214013 + 2531011;
    return *p >> 0x10;
}

f32 HSD_Randf(void)
{
    u32* p = seed_ptr();
    trace_draw(p);
    *p = *p * 214013 + 2531011;
    return (f32) (*p >> 0x10) / (1 << 16);
}

s32 HSD_Randi(s32 max_val)
{
    return max_val * HSD_Rand() / (1 << 16);
}

void _HSD_RandForgetMemory(void* low, void* high)
{
    if (low <= (void*) HSD_RandSeedPtr && (void*) HSD_RandSeedPtr < high) {
        HSD_RandSeedPtr = &seed;
    }
    return;
}
