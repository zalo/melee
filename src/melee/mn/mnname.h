#ifndef GALE01_23749C
#define GALE01_23749C

#include <sysdolphin/baselib/forward.h>

#include <stdbool.h>

#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>

#ifdef MELEE_NATIVE
/// The name menu's user data borrows the HSD_GObj field names for 13 JObj
/// slots written at a pointer stride from +8. On the GameCube that matches the
/// real HSD_GObj layout; natively the u64 gxlink_prios would break the stride,
/// so the slots are mirrored here with host-sized pointers.
typedef struct MnName_UserData {
    u16 classifier;
    u8 p_link;
    u8 gx_link;
    u8 p_priority;
    u8 render_priority;
    u8 obj_kind;
    u8 user_data_kind;
    void* next;
    void* prev;
    void* next_gx;
    void* prev_gx;
    void* proc;
    void* render_cb;
    void* gxlink_prios_lo;
    void* gxlink_prios_hi;
    void* hsd_obj;
    void* user_data;
    void* user_data_remove_func;
    void* x34_unk;
} MnName_UserData;

typedef struct MnName_GObj {
    MnName_UserData gobj;
    void* x38;
    HSD_Text* text;
    HSD_Text* text2;
} MnName_GObj;
#else
typedef struct MnName_GObj {
    /* +00 */ HSD_GObj gobj;
    /* +38 */ void* x38;
    /* +3C */ HSD_Text* text;
    /* +40 */ HSD_Text* text2;
} MnName_GObj;
#endif

/* 23749C */ char* mnName_8023749C(int slot);
/* 23754C */ char* GetNameText(int slot);
/* 237594 */ int GetNameCount(void);
/* 2375EC */ bool IsNameListFull(void);
/* 237654 */ s32 CompareNameStrings(char* str, char* slot);
/* 2377A8 */ bool IsNameUnique(char* name);
/* 237834 */ void DeleteName(u8);
/* 2379BC */ bool IsNameValid(int slot);
/* 237A04 */ void CreateNameAtIndex(s32 slot);
/* 237A68 */ void mnName_SortNames(HSD_GObj*);
/* 237D94 */ u8 mnName_80237D94(s32, u8);
/* 237F78 */ void mnName_ConfirmNameDeleteInput(HSD_GObj*);
/* 23817C */ void mnName_MainInput(HSD_GObj*);
/* 238540 */ void fn_80238540(HSD_GObj* gobj);
/* 2385A0 */ void mnName_802385A0(HSD_GObj* gobj);
/* 2385D4 */ s32 mnName_GetPageCount(void);
/* 238698 */ s32 mnName_GetColumnCount(void);
/* 238754 */ void mnName_80238754(HSD_GObj* gobj);
/* 2388D4 */ HSD_JObj* mnName_802388D4(HSD_GObj* gobj, u8 index);
/* 238964 */ f32 mnName_80238964(u8 index, u8 target, u8 flag);
/* 238A04 */ void mnName_80238A04(HSD_GObj* gobj, u8 target, u8 flag);
/* 238AE0 */ void mnName_80238AE0(HSD_GObj* gobj, u8 index, u8 arg2);
/* 238C34 */ void mnName_80238C34(HSD_GObj*, u8, u8);
/* 239574 */ void fn_80239574(HSD_GObj*);
/* 239878 */ void mnName_80239878(u8, HSD_GObj*);
/* 239A24 */ void mnName_80239A24(HSD_GObj* gobj);
/* 239EBC */ void mnName_80239EBC(HSD_JObj* jobj, f32 y);
/* 239F5C */ void mnName_80239F5C(HSD_JObj* jobj, f32 x);
/* 239FFC */ void mnName_80239FFC(HSD_GObj* gobj);
/* 23A058 */ void mnName_8023A058(HSD_GObj* gobj);
/* 23A0BC */ void fn_8023A0BC(HSD_GObj*);
/* 23A290 */ void mnName_8023A290(void);
/* 23A59C */ HSD_GObj* mnName_8023A59C(u8);
/* 23A9B4 */ void mnName_8023A9B4(u8);
/* 23AC40 */ s32 mnName_8023AC40(void);
/* 23B084 */ bool IsNameNotAllowed(char* name_idx);

#endif
