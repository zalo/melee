#include <melee/mn/types.h>
#include <melee/ft/forward.h>
#include <melee/pl/forward.h>
#include <melee/lb/lbdvd.h>
#include <melee/lb/types.h>
#include <dolphin/os.h>
#include <stdlib.h>
#include <stdio.h>
#include <sysdolphin/baselib/random.h>

static int setting(const char* name, int maximum, int current)
{
    const char* value = getenv(name);
    if (!value) return current;
    char* end;
    long parsed = strtol(value, &end, 0);
    if (!*value || *end || parsed < 0 || parsed > maximum)
        OSPanic(__FILE__, __LINE__, "Invalid matrix setting %s", name);
    return (int) parsed;
}

void MeleeNativeCheckBrinstarDamage(void);

void MeleeNativeTestConfigureVs(VsModeData* vs)
{
    if (!getenv("MELEE_MATRIX_TEST")) return;
    MeleeNativeCheckBrinstarDamage();
    // Menu and preload timing must not change the combat random sequence.
    const char* test_seed = getenv("MELEE_TEST_SEED");
    *seed_ptr = test_seed ? (u32) strtoul(test_seed, NULL, 10) : 1;
    fprintf(stderr, "[matrix] match seed=%u\n", *seed_ptr);
    vs->start.rules.time_limit = 30;
    vs->start.players[0].ckind = setting("MELEE_TEST_CHARACTER", 32, CKIND_FOX);
    vs->start.players[1].ckind = setting("MELEE_TEST_OPPONENT", 32, CKIND_MARIO);
    for (unsigned i = 0; i < 2; ++i) {
        vs->start.players[i].color = 0;
        vs->start.players[i].slot_type = Gm_PKind_Cpu;
        vs->start.players[i].cpu_level = 9;
    }
    fprintf(stderr, "[matrix] stage=%u character=%d opponent=%d\n",
            setting("MELEE_TEST_STAGE", 328, vs->start.rules.stkind), vs->start.players[0].ckind,
            vs->start.players[1].ckind);
}

void MeleeNativeTestMatchRules(struct StartMeleeRules* rules)
{
    if (!getenv("MELEE_MATRIX_TEST") || !getenv("MELEE_TEST_RESULTS")) return;
    // EnterVs reapplies menu rules after CSS/SSS. Configure the actual timed
    // match here so the Results test exercises the normal timeout path.
    rules->time_limit = 30;
    rules->timer_enabled = 1;
    rules->timer_counts_up = 0;
    rules->match_kind = 0;
    rules->is_stock = 0;
    fprintf(stderr, "[matrix] timed Results test: %u seconds\n", rules->time_limit);
}

void MeleeNativeTestStage(VsModeData* vs)
{
    if (getenv("MELEE_MATRIX_TEST"))
        vs->start.rules.stkind = setting("MELEE_TEST_STAGE", 328, vs->start.rules.stkind);
}

void MeleeNativeTestPrepareCss(CSSData* css)
{
    if (!getenv("MELEE_MATRIX_TEST")) return;
    css->vs.start.players[0].ckind = setting("MELEE_TEST_CHARACTER", 32, CKIND_FOX);
    if (css->vs.start.players[0].ckind == CKIND_SEAK)
        css->vs.start.players[0].ckind = CKIND_ZELDA;
    css->vs.start.players[0].color = 0;
    css->vs.start.players[0].slot_type = Gm_PKind_Human;
    css->vs.start.players[1].ckind = setting("MELEE_TEST_OPPONENT", 32, CKIND_MARIO);
    if (css->vs.start.players[1].ckind == CKIND_SEAK)
        css->vs.start.players[1].ckind = CKIND_ZELDA;
    css->vs.start.players[1].color = 0;
    css->vs.start.players[1].slot_type = Gm_PKind_Cpu;
}

#include <melee/pl/player.h>
#include <melee/gr/stage.h>
#include <melee/ft/types.h>
#include <melee/it/itspawn.h>
#include <sysdolphin/baselib/gobj.h>

static int matrix_scene = -1;
static unsigned matrix_frames;
void MeleeNativeMatrixScene(int scene)
{
    matrix_scene = scene;
    matrix_frames = 0;
    if (scene == 2 && getenv("MELEE_MATRIX_TEST")) {
        int expected = setting("MELEE_TEST_CHARACTER", 32, CKIND_FOX);
        int actual = Player_GetPlayerCharacter(0);
        int stage = setting("MELEE_TEST_STAGE", 328, 9);
        fprintf(stderr, "[matrix-ready] character=%d stage=%d loaded-stage=%d\n", actual, stage, Stage_80225194());
        int expected_opponent = setting("MELEE_TEST_OPPONENT", 32, CKIND_MARIO);
        int actual_opponent = Player_GetPlayerCharacter(1);
        fprintf(stderr, "[matrix-ready] opponent=%d expected-opponent=%d\n", actual_opponent, expected_opponent);
        if (actual != expected || actual_opponent != expected_opponent || Stage_80225194() != stage)
            OSPanic(__FILE__, __LINE__, "Matrix selection mismatch");
    }
}
void MeleeNativeMatrixTick(void)
{
    if (!getenv("MELEE_MATRIX_TEST") || matrix_scene != 2 ||
        !getenv("MELEE_TEST_ITEM")) return;
    ++matrix_frames;
    if (matrix_frames % 180 != 90) return;
    HSD_GObj* fighter = Player_GetEntity(0);
    if (!fighter || !fighter->user_data) return;
    Vec3 position = ((Fighter*) fighter->user_data)->cur_pos;
    position.y += 12;
    int kind = setting("MELEE_TEST_ITEM", 34, 0);
    bool spawned = it_8026D258(&position, kind);
    fprintf(stderr, "[matrix-item] kind=%d spawned=%d frame=%u\n",
            kind, spawned, matrix_frames);
}

void MeleeNativeTestPrepareSss(SSSData* sss)
{
    if (getenv("MELEE_MATRIX_TEST") && getenv("MELEE_TEST_FORCE_STAGE"))
        sss->force_stage_id = setting("MELEE_TEST_STAGE", 328, 9);
}

int MeleeNativeMatrixUnlocks(void)
{
    return getenv("MELEE_MATRIX_TEST") != NULL;
}
