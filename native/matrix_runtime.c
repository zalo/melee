#include <melee/mn/types.h>
#include <melee/ft/forward.h>
#include <melee/pl/forward.h>
#include <melee/lb/lbdvd.h>
#include <melee/lb/types.h>
#include <dolphin/os.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
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
    *HSD_RandSeedPtr = test_seed ? (u32) strtoul(test_seed, NULL, 10) : 1;
    fprintf(stderr, "[matrix] match seed=%u\n", *HSD_RandSeedPtr);
    vs->start.rules.time_limit = 30;
    vs->start.players[0].ckind = setting("MELEE_TEST_CHARACTER", 32, CKind_Fox);
    vs->start.players[1].ckind = setting("MELEE_TEST_OPPONENT", 32, CKind_Mario);
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
    css->vs.start.players[0].ckind = setting("MELEE_TEST_CHARACTER", 32, CKind_Fox);
    if (css->vs.start.players[0].ckind == CKind_Seak)
        css->vs.start.players[0].ckind = CKind_Zelda;
    css->vs.start.players[0].color = 0;
    css->vs.start.players[0].slot_type = Gm_PKind_Human;
    css->vs.start.players[1].ckind = setting("MELEE_TEST_OPPONENT", 32, CKind_Mario);
    if (css->vs.start.players[1].ckind == CKind_Seak)
        css->vs.start.players[1].ckind = CKind_Zelda;
    css->vs.start.players[1].color = 0;
    /* Online test runs: the second port is the other device's player, not a CPU. */
    css->vs.start.players[1].slot_type = getenv("MELEE_ONLINE_ROLE") ? Gm_PKind_Human : Gm_PKind_Cpu;
}

#include <melee/pl/player.h>
#include <melee/gr/stage.h>
#include <melee/ft/types.h>
#include <melee/it/itspawn.h>
#include <sysdolphin/baselib/gobj.h>

static int matrix_scene = -1;
static unsigned matrix_frames;
static unsigned freeze_frames;
void MeleeNativeMatrixScene(int scene)
{
    matrix_scene = scene;
    matrix_frames = 0;
    freeze_frames = 0;
    if (scene == 2 && getenv("MELEE_MATRIX_TEST")) {
        int expected = setting("MELEE_TEST_CHARACTER", 32, CKind_Fox);
        int actual = Player_GetPlayerCharacter(0);
        int stage = setting("MELEE_TEST_STAGE", 328, 9);
        fprintf(stderr, "[matrix-ready] character=%d stage=%d loaded-stage=%d\n", actual, stage, Stage_80225194());
        int expected_opponent = setting("MELEE_TEST_OPPONENT", 32, CKind_Mario);
        int actual_opponent = Player_GetPlayerCharacter(1);
        fprintf(stderr, "[matrix-ready] opponent=%d expected-opponent=%d\n", actual_opponent, expected_opponent);
        // Modes that pick their own fighters and stage (Event, Multi-Man, Target Test,
        // Home-Run) run without MELEE_TEST_FORCE_STAGE; only forced selections are checked.
        if (getenv("MELEE_TEST_FORCE_STAGE") &&
            (actual != expected || actual_opponent != expected_opponent || Stage_80225194() != stage))
            OSPanic(__FILE__, __LINE__, "Matrix selection mismatch");
    }
}
// Diagnostic fixed simulation state; real GX display callbacks still run.
int MeleeNativeTestFreeze(void)
{
    if (!getenv("MELEE_MATRIX_TEST") || matrix_scene != 2 ||
        !getenv("MELEE_TEST_FREEZE_AFTER")) return 0;
    unsigned limit = setting("MELEE_TEST_FREEZE_AFTER", 36000, 60);
    if (freeze_frames++ < limit) return 0;
    if (freeze_frames == limit + 1)
        fprintf(stderr, "[matrix-freeze] simulation frozen after %u updates\n", limit);
    return 1;
}

unsigned melee_native_logic_frames; // game updates, read by the VI timing line

/* Deterministic I/O: disc reads and ARAM transfers complete at the next VI pump instead of on
 * their worker threads, so the frame a load finishes on no longer depends on SD-card speed. Online
 * play needs it (both peers must spend the same number of frames in every loading screen); the
 * determinism test asks for it with MELEE_DETERMINISTIC_IO=1. */
int MeleeNativeDeterministicIO(void)
{
    static int cached = -1;
    if (cached < 0) {
        const char* v = getenv("MELEE_DETERMINISTIC_IO");
        cached = (v && *v && *v != '0') || getenv("MELEE_ONLINE_ROLE") != NULL;
    }
    return cached;
}

/* Per-frame digest of the simulation state that matters for a match: the RNG seed and, for every
 * player slot with a fighter, its position, velocity, facing, damage and motion. Pointers are left
 * out on purpose (two devices load at different addresses). Two builds of the same binary fed the
 * same inputs must agree on every frame; online play exchanges it to detect a desync, and
 * MELEE_STATE_HASH_LOG=N prints every Nth frame for the two-device determinism test. */
static u32 hash_mix(u32 h, u32 v)
{
    h ^= v;
    return h * 16777619u;
}
static u32 hash_f32(u32 h, float f)
{
    u32 bits;
    memcpy(&bits, &f, sizeof bits);
    return hash_mix(h, bits);
}
u32 MeleeNativeStateHash(void)
{
    u32 h = 2166136261u;
    int slot;
    h = hash_mix(h, *HSD_RandSeedPtr);
    for (slot = 0; slot < 4; slot++) {
        HSD_GObj* gobj = Player_GetEntity(slot);
        Fighter* fp;
        if (gobj == NULL || gobj->user_data == NULL) {
            h = hash_mix(h, 0xF0000000u | (u32) slot);
            continue;
        }
        fp = gobj->user_data;
        h = hash_mix(h, (u32) fp->kind);
        h = hash_mix(h, (u32) fp->motion_id);
        h = hash_f32(h, fp->cur_pos.x);
        h = hash_f32(h, fp->cur_pos.y);
        h = hash_f32(h, fp->cur_pos.z);
        h = hash_f32(h, fp->self_vel.x);
        h = hash_f32(h, fp->self_vel.y);
        h = hash_f32(h, fp->facing_dir);
        h = hash_f32(h, fp->dmg.x1830_percent);
        h = hash_mix(h, (u32) Player_GetStocks(slot));
    }
    return h;
}

/* Provided by netplay.cpp / audio_host.cpp in the game binary; tools that link this file alone leave
 * them null. */
void MeleeNativeNetplayFrameHash(unsigned frame, u32 hash) __attribute__((weak));
void MeleeNativeAudioTick(void) __attribute__((weak));
void MeleeNativeRunPhaseRunning(void) __attribute__((weak));

static void state_hash_tick(void)
{
    static int log_every = -1;
    u32 h;
    if (log_every < 0) {
        const char* v = getenv("MELEE_STATE_HASH_LOG");
        log_every = v ? atoi(v) : 0;
        if (log_every < 0) log_every = 0;
    }
    if (!log_every && !MeleeNativeNetplayFrameHash) return;
    h = MeleeNativeStateHash();
    if (MeleeNativeNetplayFrameHash) MeleeNativeNetplayFrameHash(melee_native_logic_frames, h);
    if (log_every && melee_native_logic_frames % (unsigned) log_every == 0)
        fprintf(stderr, "[state-hash] frame=%u hash=%08x seed=%u\n", melee_native_logic_frames, h,
                *HSD_RandSeedPtr);
}

void MeleeNativeMatrixTick(void)
{
    ++melee_native_logic_frames;
    /* Start-up is over once the game simulates: the crash-loop guard (runtime_main.cpp) only sets
     * the pipeline cache aside for runs that died before this point. */
    if (melee_native_logic_frames == 1 && MeleeNativeRunPhaseRunning) MeleeNativeRunPhaseRunning();
    /* Deterministic mode: the audio engine advances here, a fixed number of AX frames per logic
     * frame, before the digest is taken. */
    if (MeleeNativeAudioTick) MeleeNativeAudioTick();
    state_hash_tick();
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
