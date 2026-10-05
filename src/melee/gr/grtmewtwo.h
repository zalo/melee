#ifndef GALE01_2221D8
#define GALE01_2221D8

#include <melee/gr/forward.h>
#include <melee/lb/forward.h>

typedef struct grTMewtwo_UnkStruct {
#ifdef MELEE_NATIVE
    s32 x0;
    s32 x4;
    s32 xC;
    s32 x8;
    s32 x10;
    s32 x14;
    s32 x1C;
    s32 x18;
#else
    lbColl_80008D30_arg1* x0;
    lbColl_80008D30_arg1* x4;
    lbColl_80008D30_arg1* xC;
    lbColl_80008D30_arg1* x8;
    lbColl_80008D30_arg1* x10;
    lbColl_80008D30_arg1* x14;
    lbColl_80008D30_arg1* x1C;
    lbColl_80008D30_arg1* x18;
#endif
} grTMewtwo_UnkStruct;

/* 3E8FCC */ extern StageData grTMewtwo_StageData;

#endif
