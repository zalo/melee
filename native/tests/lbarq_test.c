#include "audio_host.h"
#include <melee/lb/lbarq.h>
#include <dolphin/os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char aram[4096];
OSTime OSGetTime(void) { return 0; }
u32 OSGetPhysicalMemSize(void) { return 24 * 1024 * 1024; }
void* MeleeNativeARAM(unsigned* size) { *size = sizeof(aram); return aram; }
void __assert(char* file, u32 line, char* message)
{
    OSPanic(file, line, "%s", message);
}

int main(void)
{
    unsigned char output[128];
    lbArq_80014D2C();
    /* Exercise the real asynchronous completion and repeatedly recycle nodes.
     * With an unsynchronized optimized polling loop, the first read hangs. */
    for (unsigned pass = 0; pass < 100; ++pass) {
        memset(aram + 128, pass, sizeof(output));
        memset(output, 0xff, sizeof(output));
        lbArq_80014BD0(128, output, sizeof(output), NULL, NULL);
        for (unsigned i = 0; i < sizeof(output); ++i) {
            if (output[i] != pass) abort();
        }
    }
    puts("PASS: optimized blocking ARAM reads observe worker completion and data");
}
