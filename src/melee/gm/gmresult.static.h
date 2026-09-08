#ifndef MELEE_GM_RESULT_STATIC_H
#define MELEE_GM_RESULT_STATIC_H

#include <melee/gm/types.h>

static struct ResultsData lbl_8046DBE8;
static u32 lbl_804D3F8C;
#ifdef MELEE_NATIVE
u8 lbl_804D3FA0[4] = { 0x81, 0x7C, 0, 0 };
u8 lbl_804D3FA4[4] = { 0x81, 0x7B, 0, 0 };
#else
u32 lbl_804D3FA0 = 0x817C0000;
u32 lbl_804D3FA4 = 0x817B0000;
#endif
static s8 lbl_804D3FB0 = 0x30;

#endif
