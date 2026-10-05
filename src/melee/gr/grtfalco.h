#ifndef GALE01_2207F0
#define GALE01_2207F0

#include <melee/gr/forward.h>
#include <melee/lb/forward.h>

/* 3E8974 */ extern StageData grTFc_StageData;

struct grTFalco_YakumonoParam {
#ifdef MELEE_NATIVE
    s32 unk_0;
    s32 unk_4;
    s32 unk_8;
    s32 unk_C;
#else
    lbColl_80008D30_arg1* unk_0;
    lbColl_80008D30_arg1* unk_4;
    lbColl_80008D30_arg1* unk_8;
    lbColl_80008D30_arg1* unk_C;
#endif
};

#endif
