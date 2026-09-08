#ifndef MELEE_GM_EVENTDATA_H
#define MELEE_GM_EVENTDATA_H
#include <Runtime/platform.h>
/// @todo ::PlayerInitData
typedef struct gm_801BAB40_src {
    /* 0x00 */ s8 c_kind;
    /* 0x01 */ u8 slot_type;
    /* 0x02 */ u8 stocks;
    /* 0x03 */ u8 color;
    /* 0x04 */ u8 x5;
    /* 0x05 */ u8 sub_color;
    /* 0x06 */ u8 team;
    /* 0x07 */ u8 xB;
    /* 0x08 */ u8 flags;
    /* 0x09 */ u8 xE;
    /* 0x0A */ u8 cpu_level;
    /* 0x0B */ u8 pad;
    /* 0x0C */ u16 x12;
    /* 0x0E */ u16 hp;
    /* 0x10 */ f32 x18;
    /* 0x14 */ f32 x1C;
    /* 0x18 */ f32 x20;
} gm_801BAB40_src;

struct gm_event_char_list {
    u8 c_kind[33];
};

/// Per-level match init data; shares its first two bytes' bitfield layout
/// with #StartMeleeRules.
struct gm_evinit {
    /* 0x00 */ u32 x0_0 : 3;
    /* 0x00 */ u32 x0_3 : 3;
    /* 0x00 */ u32 x0_6 : 1;
    /* 0x00 */ u32 x0_7 : 1;
    /* 0x01 */ u32 x1_0 : 1;
    /* 0x01 */ u32 x1_1 : 1;
    /* 0x01 */ u32 x1_2 : 1;
    /* 0x01 */ u32 x1_3 : 1;
    /* 0x01 */ u32 x1_4 : 1;
    /* 0x01 */ u32 x1_5 : 3;
    /* 0x02 */ u8 unk2;
    /* 0x03 */ s8 unk3;
    /* 0x04 */ s8 unk4;
    /* 0x05 */ u8 unk5;
    /* 0x06 */ u16 unk6;
    /* 0x08 */ u32 unk8;
    /* 0x0C */ u8 padC[4];
    /* 0x10 */ u64 x10;
    /* 0x18 */ s32 x18;
    /* 0x1C */ f32 x1C;
    /* 0x20 */ f32 unk20;
    /* 0x24 */ f32 unk24;
};

/// Per-round stage and opponent table, for levels with multiple rounds.
struct gm_evstage_table {
    /* 0x00 */ u8 count;
    /* 0x01 */ u8 pad1;
    /* 0x02 */ u16 stage[7];
    /* 0x10 */ struct gm_801BAB40_src* entries[5];
};

struct gm_evbonus {
    /* 0x00 */ s8 c_kind;
    /* 0x01 */ u8 x1;
    /* 0x02 */ u8 x2;
    /* 0x03 */ u8 x3;
    /* 0x04 */ u8 x4;
    /* 0x05 */ u8 x5;
    /* 0x06 */ u8 color;
    /* 0x07 */ u8 pad7;
    /* 0x08 */ f32 x8;
    /* 0x0C */ f32 xC;
    /* 0x10 */ f32 x10;
    /* 0x14 */ u8 flags;
    /* 0x15 */ u8 x15;
    /* 0x16 */ u8 x16;
    /* 0x17 */ u8 x17;
};

struct gm_804D6900_t {
    /* 0x00 */ u8 kind;
    /* 0x01 */ u8 flags; ///< top 3 bits: player count
    /* 0x02 */ u8 pad2[2];
    /* 0x04 */ struct gm_804D6900_x4_t {
        int x0;
        intptr_t x4;
    }* x4;
    /* 0x08 */ struct gm_evinit* x8;
    /* 0x0C */ struct gm_evbonus* xC;
    /* 0x10 */ struct gm_evstage_table* x10;
    /* 0x14 */ struct gm_801BAB40_src* player_init[5];
};

#endif
