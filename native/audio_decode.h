#pragma once
#include <dolphin/ax.h>
#include <stddef.h>
/* Advances one source sample, including inclusive end and ADPCM loop history.
 * Returns 0 for an invalid ARAM address/format, 1 for a decoded sample. */
int MeleeNativeDecodeSample(AXPB* pb, const u8* aram, size_t size, s16* sample);
