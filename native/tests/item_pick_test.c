#include <melee/it/itspawn.h>
#include <melee/it/it_3F14.h>
#include <melee/it/types.h>
#include <sysdolphin/baselib/memory.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

void* HSD_MemAlloc(ssize_t size) { return calloc(1, size); }
static int pick;
int HSD_Randi(int maximum) { assert(pick >= 0 && pick < maximum); return pick; }
int main(void) {
    s32 counts[64] = {0};
    counts[6] = 2;
    counts[7] = 3;
    it_804A0E30.x0 = 12345;
    it_8026CD50(counts, 3, 1.0f);
    it_8026CA4C(&it_804A0E50, counts, 3, 6, 1.0f);
    assert(it_804A0E30.x0 == 12345);
    assert(it_804A0E50.size == 2 && it_804A0E50.x8 == 5);
    assert(it_804A0E50.x4[0] == 6 && it_804A0E50.x4[1] == 7);
    assert(it_804A0E50.xC[0] == 0 && it_804A0E50.xC[1] == 2);
    for (pick = 0; pick < 5; ++pick)
        assert(it_8026C65C(&it_804A0E50) == (pick < 2 ? 6 : 7));
    free(it_804A0E50.x4);
    free(it_804A0E50.xC);
}
