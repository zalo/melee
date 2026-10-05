#ifndef GALE01_219C98
#define GALE01_219C98

#include <Runtime/platform.h>

#include <melee/gr/forward.h>

/* 3E7E38 */ extern StageData grNBa_StageData;

/** @var ::grBattle_YakumonoParam::bg_curr_color_overlay
 * @todo ::ColorOverlay_x8_t, from ::grMaterial_801C9604
 * @var ::grBattle_YakumonoParam::bg_prev_color_overlay
 * @copydoc ::grBattle_YakumonoParam::bg_curr_color_overlay
 */
struct grBattle_YakumonoParam {
#ifdef MELEE_NATIVE
    // Serialized 32-bit slots; resolved with GR_MATERIAL_SCRIPT.
    u32 bg_curr_color_overlay;
    u32 bg_prev_color_overlay;
#else
    void* bg_curr_color_overlay;
    void* bg_prev_color_overlay;
#endif
};
#ifdef MELEE_NATIVE
// The block is read in place at its GameCube offsets (native/stage_numeric_layouts.hpp).
STATIC_ASSERT(sizeof(struct grBattle_YakumonoParam) == 8);
#endif

#endif
