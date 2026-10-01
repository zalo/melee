/* Unattended play of whole game modes with a CPU-driven player 1 (MELEE_TEST_P1_CPU, matrix_runtime.c).
 *
 * The CPU AI plays every match. This file is the rest of the player: it decides what the pad does on
 * every screen once the input script has reached the title or a menu (keyboard_input.cpp polls
 * MeleeNativePuppetPad once per logic frame), and with MELEE_TEST_MODES it also picks the modes.
 *
 *   MELEE_TEST_MODES=<name[:count],...>  playlist, repeated for as long as the run lasts. Names are in
 *       kModes below; count is how many units of the mode to play before moving on (a run of
 *       Classic/Adventure/All-Star, one event, one match of everything else). Each mode is entered
 *       from the main menu the way its menu entry does it, so no menu path has to be scripted and
 *       nothing depends on what the save has unlocked. Player 1's character advances on every
 *       character select; MELEE_TEST_CHARACTER is the first one.
 *   MELEE_TEST_EVENT=<0..50>             first event of the "event" mode (default 0, Event 1).
 *   MELEE_TEST_EVENT_REPEAT=1            plays that one event again and again instead of moving on.
 *   MELEE_TEST_MATCH_SECONDS=<n>         overrides the per-mode time after which a match that has not
 *       ended is left with the pause menu's L+R+A+START.
 *   MELEE_TEST_ROUTE_SECONDS=<n>         time the puppet gets to walk a side-scrolling stage before
 *       the stage is declared cleared (default 75).
 *
 * Stages without an opponent are the ones the AI cannot play: it stands still. There the pad keeps the
 * fighter (MeleeNativePuppetWalks): Break the Targets is left by running off the stage, and the
 * Adventure side-scrollers and the All-Star rest area are walked toward their goal. The walker is no
 * platformer; when its time is up the goal flag is set for it, so every later stage of the mode still
 * gets loaded and played. Everything the harness forces is logged as "[puppet] ...". */
#include <melee/mn/types.h>
#include <melee/ft/forward.h>
#include <melee/ft/types.h>
#include <melee/ft/ftlib.h>
#include <melee/gm/forward.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_16A2.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmevent.h>
#include <melee/gm/gmvs.h>
#include <melee/gr/ground.h>
#include <melee/gr/stage.h>
#include <melee/it/forward.h>
#include <melee/it/types.h>
#include <melee/mn/mnmain.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/gobj.h>
#include <dolphin/os.h>
#include <dolphin/pad.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int MeleeNativePuppetLevel(int slot);
int MeleeNativePuppetStageClear(void);
Fighter* MeleeNativeLiveFighter(int slot);

enum { kRun, kEvent, kMatch };
struct PuppetMode {
    const char* name;
    u8 mode;      /* GM_* */
    u8 family;    /* kRun: ends at the menu; kEvent: one event, then the menu; kMatch: back to its CSS */
    u16 units;    /* default count */
    u16 seconds;  /* a match still running after this long is abandoned */
};
static const struct PuppetMode kModes[] = {
    { "classic", GM_CLASSIC, kRun, 1, 600 },
    { "adventure", GM_ADVENTURE, kRun, 1, 600 },
    { "allstar", GM_ALLSTAR, kRun, 1, 600 },
    { "event", GM_EVENT, kEvent, 51, 240 },
    { "targets", GM_TARGET_TEST, kMatch, 25, 40 },
    { "homerun", GM_HOME_RUN_CONTEST, kMatch, 3, 120 },
    { "10man", GM_10MAN_VS, kMatch, 2, 300 },
    { "100man", GM_100MAN_VS, kMatch, 1, 600 },
    { "3min", GM_3MIN_VS, kMatch, 1, 240 },
    { "15min", GM_15MIN_VS, kMatch, 1, 960 },
    { "endless", GM_ENDLESS_VS, kMatch, 1, 300 },
    { "cruel", GM_CRUEL_VS, kMatch, 2, 300 },
    { "training", GM_TRAINING, kMatch, 3, 45 },
    { "vs", GM_VS, kMatch, 4, 600 },
    { "stamina", GM_STAMINA_VS, kMatch, 2, 600 },
    { "suddendeath", GM_SUPER_SUDDEN_DEATH_VS, kMatch, 2, 600 },
    { "giant", GM_GIANT_VS, kMatch, 2, 600 },
    { "tiny", GM_TINY_VS, kMatch, 2, 600 },
    { "invisible", GM_INVISIBLE_VS, kMatch, 2, 600 },
    { "fixedcamera", GM_CAMERA_VS, kMatch, 2, 600 },
    { "singlebutton", GM_SINGLE_BUTTON_VS, kMatch, 2, 600 },
    { "lightning", GM_LIGHTNING_VS, kMatch, 2, 600 },
    { "slomo", GM_SLOMO_VS, kMatch, 2, 600 },
};

struct PlaylistItem {
    const struct PuppetMode* mode;
    int units;
};
static struct PlaylistItem playlist[48];
static int playlist_len = -1;
static int item_index, units_done, launched, laps;
static int event_index = -1;

static int env_int(const char* name, int minimum, int maximum, int fallback)
{
    const char* value = getenv(name);
    char* end;
    long parsed;
    if (!value) return fallback;
    parsed = strtol(value, &end, 0);
    if (!*value || *end || parsed < minimum || parsed > maximum)
        OSPanic(__FILE__, __LINE__, "Invalid puppet setting %s", name);
    return (int) parsed;
}

static void parse_playlist(void)
{
    const char* text = getenv("MELEE_TEST_MODES");
    playlist_len = 0;
    if (!text || !MeleeNativePuppetLevel(0)) return;
    while (*text) {
        size_t len = strcspn(text, ",:");
        const struct PuppetMode* found = NULL;
        size_t i;
        for (i = 0; i < sizeof kModes / sizeof *kModes; ++i)
            if (strlen(kModes[i].name) == len && !strncmp(kModes[i].name, text, len)) found = &kModes[i];
        if (!found || playlist_len == (int) (sizeof playlist / sizeof *playlist))
            OSPanic(__FILE__, __LINE__, "Invalid MELEE_TEST_MODES entry at \"%s\"", text);
        playlist[playlist_len].mode = found;
        playlist[playlist_len].units = found->units;
        text += len;
        if (*text == ':') {
            char* end;
            long count = strtol(text + 1, &end, 10);
            if (count < 1 || count > 10000) OSPanic(__FILE__, __LINE__, "Invalid MELEE_TEST_MODES count");
            playlist[playlist_len].units = (int) count;
            text = end;
        }
        ++playlist_len;
        if (*text == ',') ++text;
        else if (*text) OSPanic(__FILE__, __LINE__, "Invalid MELEE_TEST_MODES entry at \"%s\"", text);
    }
}

/* True when a playlist drives the menus (and not only the screens between matches). */
int MeleeNativePuppetDirects(void)
{
    if (playlist_len < 0) parse_playlist();
    return playlist_len > 0;
}

/* Every character select of a playlist run gets the next character, so a long run covers the cast
 * (and with it every Break the Targets stage). */
static int next_character(void)
{
    static int next = -1;
    int i;
    if (next < 0) next = env_int("MELEE_TEST_CHARACTER", 0, CKind_Playable_Count - 1, 0);
    for (i = 0; i < CKind_Playable_Count; ++i) {
        int ckind = (next + i) % CKind_Playable_Count;
        /* Sheik has no icon of her own. */
        if (ckind != CKind_Seak && gm_IsCKindUnlocked(ckind)) {
            next = ckind + 1;
            return ckind;
        }
    }
    return CKind_Mario;
}

/* 1-P character selects (gm_801B06B0): the remembered character is replaced with the next one, which
 * also places the token, so START alone begins the mode. */
int MeleeNativePuppetCharacter(int ckind)
{
    return MeleeNativePuppetDirects() ? next_character() : ckind;
}

/* Versus character selects: player 1 against one level 9 CPU. */
int MeleeNativePuppetPrepareVsCss(CSSData* css)
{
    int i;
    if (!MeleeNativePuppetDirects()) return 0;
    for (i = 0; i < 2; ++i) {
        css->vs.start.players[i].ckind = next_character();
        css->vs.start.players[i].color = 0;
        css->vs.start.players[i].slot_type = i == 0 ? Gm_PKind_Human : Gm_PKind_Cpu;
        css->vs.start.players[i].cpu_level = 9;
    }
    for (; i < 4; ++i) css->vs.start.players[i].slot_type = Gm_PKind_NA;
    return 1;
}

/* Who moves player 1's fighter in the current match. */
enum { kDriveAi, kDriveLeft, kDriveRoute };
static int has_opponent(void)
{
    int slot;
    if (gm_8016A1F8()) return 1;
    for (slot = 1; slot < 6; ++slot) {
        Fighter* fp = MeleeNativeLiveFighter(slot);
        if (fp != NULL && !ftLib_IsSleeping(fp->gobj)) return 1;
    }
    return 0;
}
static int drive_kind(void)
{
    StKind stage;
    GrKind kind;
    if (!MeleeNativePuppetLevel(0)) return kDriveAi;
    stage = Stage_80225194();
    kind = Stage_8022519C(stage);
    /* Break the Targets: no opponent and no goal the walker could reach; run off the stage. */
    if (kind >= Gr_Kind_TMario && kind <= Gr_Kind_TGanon) return kDriveLeft;
    if (kind == Gr_Kind_Heal) return kDriveRoute;
    if ((kind >= Gr_Kind_KinokoRoute && kind <= Gr_Kind_BigBlueRoute) ||
        (kind == Gr_Kind_Icemt && gm_GetCurrentGameMode() == GM_ADVENTURE))
        return has_opponent() ? kDriveAi : kDriveRoute;
    return kDriveAi;
}
/* ftCo_IsCpuControlled asks this: non-zero leaves player 1's fighter to the pad. */
int MeleeNativePuppetWalks(void)
{
    return drive_kind() != kDriveAi;
}

static int scene_now = -2;
static unsigned scene_serial, scene_serial_seen;
static unsigned scene_tick, match_frames, clear_tick;
static int route_stage = -1, forced_clear;
static unsigned route_frames;
static int match_stage = -1;
static struct {
    float ground_y, probe_x;
    unsigned jump_hold, jump_rest, up_b_hold;
    int stuck, up_b_used, drive;
    float hurt;
} walker;

static const struct PuppetMode* current_mode(void)
{
    return MeleeNativePuppetDirects() ? playlist[item_index].mode : NULL;
}

static unsigned match_limit(void)
{
    static int forced = -2;
    const struct PuppetMode* mode = current_mode();
    if (forced == -2) forced = env_int("MELEE_TEST_MATCH_SECONDS", 1, 36000, -1);
    if (forced > 0) return (unsigned) forced * 60;
    return mode != NULL ? mode->seconds * 60u : 0;
}

/* Every screen that only wants to be dismissed: A and START alternate. */
static u16 press_through(unsigned tick, int with_a)
{
    unsigned phase = tick % 60;
    if (phase >= 30 && phase < 36) return PAD_BUTTON_START;
    if (phase < 6 && with_a) return PAD_BUTTON_A;
    return 0;
}

/* Item kinds that existed during the match (items, Pokemon, stage hazards, enemies): what the match
 * exercised, for reading a run's coverage out of its log. */
static unsigned char items_seen[512 / 8];

static void note_items(void)
{
    HSD_GObj* gobj;
    for (gobj = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_ITEM]; gobj != NULL; gobj = gobj->next) {
        unsigned kind = (unsigned) ((Item*) gobj->user_data)->kind;
        if (kind < 512) items_seen[kind / 8] |= (unsigned char) (1u << (kind % 8));
    }
}

static void report_items(void)
{
    char line[1200];
    size_t used = 0;
    unsigned kind;
    for (kind = 0; kind < 512 && used + 8 < sizeof line; ++kind)
        if (items_seen[kind / 8] & (1u << (kind % 8))) used += (size_t) sprintf(line + used, " %u", kind);
    if (used) fprintf(stderr, "[puppet] item kinds seen:%s\n", line);
    memset(items_seen, 0, sizeof items_seen);
}

static void enter_scene(int scene)
{
    const struct PuppetMode* mode = current_mode();
    report_items();
    scene_tick = match_frames = clear_tick = 0;
    forced_clear = 0;
    memset(&walker, 0, sizeof walker);
    walker.drive = -1;
    if (scene < 0) return;
    fprintf(stderr, "[puppet] scene %d\n", scene);
    if ((scene == GS_VS || scene == GS_TRAINING) && mode != NULL && mode->family == kMatch &&
        gm_GetCurrentGameMode() == mode->mode)
        ++units_done;
}

/* Main menu with a playlist: account for what was just played and start the next unit, the way the
 * menu entry itself does (record the port, hand the mode to the menu's exit). */
static void menu_director(unsigned tick)
{
    static int menu_launch;
    const struct PlaylistItem* item;
    if (tick == 0) menu_launch = 0;
    if (tick % 600 != 40) return;
    item = &playlist[item_index];
    if (!menu_launch && launched) {
        if (item->mode->family != kMatch) ++units_done;
        if (item->mode->family == kEvent && !env_int("MELEE_TEST_EVENT_REPEAT", 0, 1, 0)) ++event_index;
    }
    if (!menu_launch && units_done >= item->units) {
        fprintf(stderr, "[puppet] mode %s done (%d units)\n", item->mode->name, units_done);
        units_done = 0;
        if (++item_index == playlist_len) {
            item_index = 0;
            fprintf(stderr, "[puppet] playlist lap %d complete\n", ++laps);
        }
        item = &playlist[item_index];
    }
    if (item->mode->family == kEvent) {
        if (event_index < 0) event_index = env_int("MELEE_TEST_EVENT", 0, 50, 0);
        event_index %= 51;
        gm_801BEB74((u8) event_index);
        fprintf(stderr, "[puppet] mode event %d (unit %d of %d)\n", event_index + 1, units_done + 1, item->units);
    } else {
        fprintf(stderr, "[puppet] mode %s (unit %d of %d)\n", item->mode->name, units_done + 1, item->units);
    }
    gm_801677E8(0);
    mn_80229860((s8) item->mode->mode);
    menu_launch = launched = 1;
}

static void css_pad(unsigned tick, u16* buttons, s8* x, s8* y)
{
    const struct PuppetMode* mode = current_mode();
    if (mode != NULL && mode->family == kMatch && gm_GetCurrentGameMode() == mode->mode &&
        units_done >= playlist[item_index].units) {
        /* These modes return to their character select forever; L+R+START is the way to the menu. */
        unsigned phase = tick % 120;
        if (phase >= 20 && phase < 60) *buttons = PAD_TRIGGER_L | PAD_TRIGGER_R;
        if (phase >= 40 && phase < 46) *buttons |= PAD_BUTTON_START;
        return;
    }
    if (tick >= 900 && (tick - 900) % 600 < 110) {
        /* START has not started anything for 15 s: no character is on the token. Pick the one the
         * hand reaches with the same moves the menu scripts use. */
        unsigned phase = (tick - 900) % 600;
        if (phase == 0) fprintf(stderr, "[puppet] character select not ready; picking by hand\n");
        if (phase < 45) *y = 80;
        else if (phase < 55) *x = 80;
        else if (phase < 63) *y = -80;
        else if (phase < 67) *x = -80;
        else if (phase >= 97 && phase < 103) *buttons = PAD_BUTTON_A;
        return;
    }
    /* A would pick the token back up. */
    *buttons = press_through(tick, 0);
}

/* Leaves a match through the pause menu. Retried, since pausing can be refused for a while (a boss
 * entrance, a fighter that is being revived). */
static void quit_pad(unsigned frames, u16* buttons)
{
    unsigned phase = frames % 300;
    if (frames == 0)
        fprintf(stderr, "[puppet] match still running after %u s; leaving it with L+R+A+START\n", match_frames / 60);
    if (phase < 6) *buttons = PAD_BUTTON_START;
    else if (phase >= 30 && phase < 60) {
        *buttons = PAD_TRIGGER_L | PAD_TRIGGER_R | PAD_BUTTON_A;
        if (phase >= 42 && phase < 48) *buttons |= PAD_BUTTON_START;
    }
}

/* The goal of a side-scrolling stage: the nearest of the goal joints the stage's own check scans
 * (Ground_801C0C2C). Stages without one (Icicle Mountain) are climbed. */
static int nearest_goal(const Vec3* from, Vec3* goal)
{
    int i, found = 0;
    float best = 0;
    for (i = 0x99; i < 0xB3; ++i) {
        Vec3 pos;
        float dx, dy, dist;
        if (!Ground_801C2D24(i, &pos)) continue;
        dx = pos.x - from->x;
        dy = pos.y - from->y;
        dist = dx * dx + dy * dy;
        if (!found || dist < best) {
            best = dist;
            *goal = pos;
            found = 1;
        }
    }
    return found;
}

/* All-Star's rest area: a damaged fighter takes one of the Heart Containers before the exit, or the
 * run ends after the first few fights and the later ones are never loaded. */
static int nearest_heart(const Fighter* fp, Vec3* goal)
{
    HSD_GObj* cur;
    int found = 0;
    float best = 0;
    if (fp->dmg.x1830_percent < 60.0f || HSD_GObjPLinkHead == NULL) return 0;
    if (Stage_8022519C(Stage_80225194()) != Gr_Kind_Heal) return 0;
    for (cur = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_ITEM]; cur != NULL; cur = cur->next) {
        const Item* ip = cur->user_data;
        float dx;
        if (cur->classifier != HSD_GOBJ_CLASS_ITEM || ip == NULL || ip->kind != It_Kind_Heart) continue;
        dx = fabsf(ip->pos.x - fp->cur_pos.x);
        if (!found || dx < best) {
            best = dx;
            *goal = ip->pos;
            found = 1;
        }
    }
    return found;
}

static void route_pad(Fighter* fp, u16* buttons, s8* x, s8* y)
{
    static unsigned budget;
    Vec3 goal;
    float dx, dy;
    int stage = Stage_80225194();
    int grounded = fp->ground_or_air == GA_Ground;
    if (!budget) budget = (unsigned) env_int("MELEE_TEST_ROUTE_SECONDS", 1, 3600, 75) * 60;
    if (stage != route_stage) {
        /* Counted per stage and not per attempt: a stage that keeps killing the walker is retried
         * after Continue, and the time must still run out. */
        route_stage = stage;
        route_frames = 0;
    }
    if (++route_frames > budget && !forced_clear) {
        forced_clear = 1;
        fprintf(stderr, "[puppet] stage %d not cleared after %u s of walking; forcing the goal\n", stage,
                budget / 60);
    }
    if (walker.hurt > 0.0f && fp->dmg.x1830_percent < walker.hurt * 0.5f) {
        fprintf(stderr, "[puppet] stage %d: healed from %.0f%%\n", stage, walker.hurt);
        walker.hurt = 0.0f;
    }
    if (nearest_heart(fp, &goal)) {
        walker.hurt = fp->dmg.x1830_percent;
        dx = goal.x - fp->cur_pos.x;
        dy = goal.y - fp->cur_pos.y;
        if (fabsf(dx) < 5.0f && fabsf(dy) < 12.0f) {
            /* Standing on it: A picks it up. */
            if (grounded && route_frames % 20 < 2) *buttons |= PAD_BUTTON_A;
            return;
        }
    } else if (nearest_goal(&fp->cur_pos, &goal)) {
        dx = goal.x - fp->cur_pos.x;
        dy = goal.y - fp->cur_pos.y;
    } else if (Stage_8022519C(stage) == Gr_Kind_Icemt) {
        dx = (route_frames / 180) % 2 ? -1000.0f : 1000.0f;
        dy = 1000.0f;
    } else {
        dx = 1000.0f;
        dy = 0.0f;
    }
    if (dx > 4.0f) *x = 100;
    else if (dx < -4.0f) *x = -100;

    if (walker.up_b_hold) {
        --walker.up_b_hold;
        *y = 127;
        *buttons |= PAD_BUTTON_B;
        return;
    }
    if (walker.jump_hold) {
        --walker.jump_hold;
        *buttons |= PAD_BUTTON_X;
        return;
    }
    if (walker.jump_rest) {
        --walker.jump_rest;
        return;
    }
    if (grounded) {
        walker.ground_y = fp->cur_pos.y;
        walker.up_b_used = 0;
        if (route_frames % 30 == 0) {
            walker.stuck = fabsf(fp->cur_pos.x - walker.probe_x) < 3.0f && fabsf(dx) > 4.0f;
            walker.probe_x = fp->cur_pos.x;
        }
        if (walker.stuck || dy > 12.0f) {
            walker.stuck = 0;
            walker.jump_hold = 14;
            walker.jump_rest = 6;
            *buttons |= PAD_BUTTON_X;
        } else if (route_frames % 90 < 2) {
            /* Whatever stands in the way. */
            *buttons |= PAD_BUTTON_A;
        }
    } else if (fp->self_vel.y <= 0.0f && (dy > 6.0f || fp->cur_pos.y < walker.ground_y - 8.0f)) {
        if (fp->x1968_jumpsUsed < fp->co_attrs.max_jumps) {
            walker.jump_hold = 14;
            walker.jump_rest = 6;
            *buttons |= PAD_BUTTON_X;
        } else if (!walker.up_b_used) {
            walker.up_b_used = 1;
            walker.up_b_hold = 4;
        }
    }
}

static void match_pad(int scene, u16* buttons, s8* x, s8* y)
{
    unsigned limit = match_limit();
    int drive;
    if (scene == GS_VS && MeleeNativePuppetStageClear()) {
        /* The 1-P stage-clear score screen is part of the match scene and waits for START; in every
         * other state of the scene START would pause the match. */
        *buttons = press_through(clear_tick++, 1);
        return;
    }
    if (++match_frames == 1) {
        /* A stage that comes back after another one (the All-Star rest area after every fight)
         * gets its walking time again; the same stage again is a retry. */
        int stage = Stage_80225194();
        if (stage != match_stage) route_stage = -1;
        match_stage = stage;
    }
    note_items();
    drive = drive_kind();
    if (drive != walker.drive) {
        static const char* const names[] = { "the CPU", "the pad, to the left", "the pad, toward the goal" };
        if (walker.drive >= 0 || drive != kDriveAi)
            fprintf(stderr, "[puppet] stage %d frame %u: fighter driven by %s\n", Stage_80225194(), match_frames,
                    names[drive]);
        walker.drive = drive;
    }
    if (drive == kDriveLeft) {
        *x = -80;
    } else if (drive == kDriveRoute) {
        Fighter* fp = MeleeNativeLiveFighter(0);
        if (fp != NULL && !ftLib_IsSleeping(fp->gobj)) route_pad(fp, buttons, x, y);
    }
    if (limit && match_frames > limit && !forced_clear) {
        *x = *y = 0;
        *buttons = 0;
        quit_pad(match_frames - limit - 1, buttons);
    }
}

/* Training never ends: after its time, START opens the training menu, Up wraps to its last row
 * (Exit) and A takes it. If that has not worked after a few rounds the row's effect is applied. */
static void training_pad(unsigned tick, u16* buttons, s8* y)
{
    unsigned limit = match_limit() ? match_limit() : 45 * 60;
    unsigned phase, round;
    if (tick < limit) return;
    phase = (tick - limit) % 240;
    round = (tick - limit) / 240;
    if (round >= 3) {
        if (phase == 0) {
            fprintf(stderr, "[puppet] training menu exit did not work; ending the session directly\n");
            gm_8016B328();
        }
        return;
    }
    if (phase < 6) *buttons = PAD_BUTTON_START;
    else if (phase >= 40 && phase < 46) *y = 80;
    else if (phase >= 70 && phase < 76) *buttons = PAD_BUTTON_A;
}

/* Called by the native collision tripwires: which fighter went non-finite, and doing what. */
void MeleeNativeReportFighter(HSD_GObj* gobj)
{
    const Fighter* fp = gobj->user_data;
    fprintf(stderr,
            "[fighter] slot=%d kind=%d motion=%d anim=%d facing=%g scale=(%g,%g,%g) pos=(%g,%g,%g) "
            "vel=(%g,%g,%g) percent=%g\n",
            fp->player_idx, (int) fp->kind, (int) fp->motion_id, (int) fp->anim_id, fp->facing_dir,
            fp->x34_scale.x, fp->x34_scale.y, fp->x34_scale.z, fp->cur_pos.x, fp->cur_pos.y, fp->cur_pos.z,
            fp->self_vel.x, fp->self_vel.y, fp->self_vel.z, fp->dmg.x1830_percent);
}

/* Every scene start (MeleeNativeMatrixScene). Consecutive stages of Adventure and All-Star are the
 * same scene kind with nothing in between, and each is a match of its own. */
void MeleeNativePuppetScene(void)
{
    ++scene_serial;
    /* Not left for the first pad poll: the next stage's first frames would be cleared as well. */
    forced_clear = 0;
}

/* One call per logic frame once the input script is done: the pad for this frame. */
void MeleeNativePuppetPad(int scene, u16* buttons, s8* x, s8* y)
{
    unsigned tick;
    *buttons = 0;
    *x = *y = 0;
    if (scene != scene_now || scene_serial != scene_serial_seen) {
        scene_now = scene;
        scene_serial_seen = scene_serial;
        enter_scene(scene);
    }
    if (scene < 0) return; /* loading */
    tick = scene_tick++;
    switch (scene) {
    case GS_VS:
    case GS_SUDDEN_DEATH:
        match_pad(scene, buttons, x, y);
        return;
    case GS_TRAINING:
        training_pad(tick, buttons, y);
        return;
    case GS_CSS:
        css_pad(tick, buttons, x, y);
        return;
    case GS_MENU:
        if (MeleeNativePuppetDirects()) {
            menu_director(tick);
            return;
        }
        break;
    }
    *buttons = press_through(tick, 1);
}

/* After the frame's procs (MeleeNativeMatrixTick): a stage whose walking time ran out is cleared here,
 * behind the stage's own goal check, which rewrites the flag every frame. */
void MeleeNativePuppetTick(void)
{
    if (forced_clear && (scene_now == GS_VS || scene_now == GS_SUDDEN_DEATH)) Ground_801C1D6C(0x10);
}
