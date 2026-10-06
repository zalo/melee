/* Universal Controller Fix 0.84 (see include/melee_ucf.h).
 *
 * UCF 0.84 is eight code injections into the retail game. Each rule below names the injection
 * it reproduces by its GameCube address (NTSC 1.02) and the game function that contains it;
 * conditions and thresholds are the ones the injected machine code tests.
 *
 * Arithmetic is ordinary float arithmetic. The reference uses PowerPC fused multiply-add and
 * multiply-subtract in two places (the rim test and the SDI magnitude); the notes there say why
 * the unfused result decides the same way for every position a controller can report.
 *
 * Two deliberate differences from the injected code, both outside what it defines:
 *  - it keeps its history table forever; here it is cleared whenever the fighter system is
 *    initialised for a scene, so a match never starts from what a previous one left behind
 *    (which matters for a port that a human played and a CPU plays next: nothing feeds it);
 *  - it indexes its table with any port number; here ports outside 0..3 have no history. */
#include "include/melee_ucf.h"
#include "include/melee_settings.h"
#include <string.h>

/* Defined by the online-play runtime; absent from the unit test's link. */
extern int MeleeNativeNetplayActive(void) __attribute__((weak));

/* Raw units of stick travel over two frames that count as a deliberate flick, per rule. */
#define DASH_BACK_TRAVEL 75
#define TUMBLE_TRAVEL 75
#define SDI_TRAVEL 62
#define DROP_TRAVEL 44

struct stick_history {
    signed char x[4], y[4];    /* ring of raw samples */
    unsigned char newest;      /* ring index of the latest sample */
    unsigned char drop_frames; /* see MeleeNativeUcfTrackDrop */
};
static struct stick_history history[MELEE_UCF_PORTS];

static int known_port(int port) {
    return port >= 0 && port < MELEE_UCF_PORTS;
}

int MeleeNativeUcfActive(void) {
    if (!MeleeNativeUcf()) return 0;
    if (MeleeNativeNetplayActive && MeleeNativeNetplayActive()) return 0;
    return 1;
}

void MeleeNativeUcfReset(void) {
    memset(history, 0, sizeof(history));
}

void MeleeNativeUcfRecord(int port, int raw_x, int raw_y) {
    struct stick_history* h;
    if (!MeleeNativeUcfActive() || !known_port(port)) return;
    h = &history[port];
    h->newest = (h->newest + 1) & 3;
    h->x[h->newest] = (signed char) raw_x;
    h->y[h->newest] = (signed char) raw_y;
}

/* Two frames, not one: a stick that is read while it is still moving reports an in-between
 * value on the frame before it arrives, and the rules want the whole movement. */
int MeleeNativeUcfTravelX(int port) {
    const struct stick_history* h;
    if (!known_port(port)) return 0;
    h = &history[port];
    return h->x[h->newest] - h->x[(h->newest + 2) & 3];
}

int MeleeNativeUcfTravelY(int port) {
    const struct stick_history* h;
    if (!known_port(port)) return 0;
    h = &history[port];
    return h->y[h->newest] - h->y[(h->newest + 2) & 3];
}

static int square(int value) {
    return value * value;
}

/* One axis of the rim test, in raw units (80 per 1.0) and rounded outward: a reading of k units
 * counts as k + 1, and a centred or 1-unit reading as 2. The 0.0001 keeps a product that lands a
 * hair above or below the whole number k on the same side of the truncation, which is also why
 * it does not matter that the reference computes |v| * 80 - 0.0001 as one fused operation: the
 * two differ by far less than the margin for any k / 80 a pad can produce. */
static int rim_units(float v) {
    if (v < 0.0f) v = -v;
    return (int) (v * 80.0f - 0.0001f) + 2;
}

/* The game clamps the stick to a circle of radius 80, so "on the rim" is: pushed as far as the
 * gate allows in this direction. With the outward rounding above that is distance > 80. */
int MeleeNativeUcfOnRim(float x, float y) {
    return square(rim_units(x)) + square(rim_units(y)) > 80 * 80;
}

/* 8006B460 (Fighter_procInput), first half. A stick held against a cardinal notch should read as
 * a full, exact cardinal even when the controller reports it a few units off axis: at least 80
 * along one axis and no more than 6 off it becomes exactly +-1.0 and 0. The horizontal axis is
 * tried first; a raw value past 80 on it with more than 6 on the other is left alone. */
int MeleeNativeUcfSnapCardinal(int raw_x, int raw_y, float* x, float* y) {
    if (raw_x >= 80 || raw_x <= -80) {
        if (raw_y < -6 || raw_y > 6) return 0;
        *x = raw_x < 0 ? -1.0f : 1.0f;
        *y = 0.0f;
        return 1;
    }
    if (raw_y >= 80 || raw_y <= -80) {
        if (raw_x < -6 || raw_x > 6) return 0;
        *x = 0.0f;
        *y = raw_y < 0 ? -1.0f : 1.0f;
        return 1;
    }
    return 0;
}

/* 8006B460 (Fighter_procInput), second half. Counts the frames a shield-drop flick has been
 * held: the stick is on the rim at or below -0.609375 (about 37.5 degrees below horizontal on
 * the rim), and it got there by moving more than 44 raw units downward or upward within two
 * frames while the game's own vertical tilt timer was still at 0 or 1. Once started the count
 * keeps running for as long as the stick stays in that zone and returns to 0 when it leaves.
 * The count is one byte in the reference and wraps from 255 to 0; so does this one. */
void MeleeNativeUcfTrackDrop(int port, float x, float y, int y_tilt_frames) {
    struct stick_history* h;
    if (!MeleeNativeUcfActive() || !known_port(port)) return;
    h = &history[port];
    if (y > -0.609375f || !MeleeNativeUcfOnRim(x, y)) {
        h->drop_frames = 0;
    } else if (h->drop_frames != 0) {
        h->drop_frames++;
    } else if (y_tilt_frames <= 1 && square(MeleeNativeUcfTravelY(port)) > square(DROP_TRAVEL)) {
        h->drop_frames = 1;
    }
}

int MeleeNativeUcfDropFrames(int port) {
    return known_port(port) ? history[port].drop_frames : 0;
}

/* 800C9A44 (ftCo_Turn_IASA). The game only dashes back when the stick crosses from neutral to
 * the dash threshold within one frame; a stick caught halfway on that frame produces the slow
 * turnaround instead. UCF accepts the dash on frame 2 of the turn when the stick is past the
 * dash threshold in the new facing direction, the horizontal tilt timer is at most 1, and the
 * raw stick moved more than 75 units horizontally over two frames.
 * stick_toward_facing is stick x times the facing direction after the turn. */
int MeleeNativeUcfDashBackRule(int port, float anim_frame, float stick_toward_facing, float dash_threshold,
                               int x_tilt_frames) {
    if (!MeleeNativeUcfActive()) return 0;
    if (anim_frame != 2.0f) return 0;
    if (stick_toward_facing < dash_threshold) return 0;
    if (x_tilt_frames > 1) return 0;
    return square(MeleeNativeUcfTravelX(port)) > square(DASH_BACK_TRAVEL);
}

/* 800D65EC (ftCo_SquatRv_CheckInput). The crouch ends when stick y rises above minus a
 * threshold. On the frame the stick first tilts horizontally (tilt timer 0) while it is on the
 * rim, UCF uses 0.59 in place of the game's value (0.625 on the retail disc): a stick rolled
 * along the rim from down toward a side has to come up a little further before the crouch ends. */
float MeleeNativeUcfSquatRvRule(float retail_threshold, int x_tilt_frames, float x, float y) {
    if (!MeleeNativeUcfActive()) return retail_threshold;
    if (x_tilt_frames < 1 && MeleeNativeUcfOnRim(x, y)) return 0.59f;
    return retail_threshold;
}

/* 8008E54C (ftCo_Damage_OnEveryHitlag). An SDI step needs the stick to have left the dead zone
 * within the SDI window. UCF adds a second way in: either axis has been out of its dead zone
 * for at most one frame (the timers SDI itself does not reset), the previous frame's stick was
 * closer to the centre than the SDI minimum, and the raw stick moved more than 62 units over two
 * frames. The reference forms the previous magnitude with a fused multiply-add; the only pad
 * position on that boundary is exactly the minimum on one axis and 0 on the other, where fused
 * and unfused agree. */
int MeleeNativeUcfSdiRule(int port, int x_held_frames, int y_held_frames, float prev_x, float prev_y,
                          float min_magnitude) {
    if (!MeleeNativeUcfActive()) return 0;
    if (x_held_frames > 1 && y_held_frames > 1) return 0;
    if (!(min_magnitude * min_magnitude > prev_x * prev_x + prev_y * prev_y)) return 0;
    return square(MeleeNativeUcfTravelX(port)) + square(MeleeNativeUcfTravelY(port)) > square(SDI_TRAVEL);
}

/* 80093294 (ftCo_80093240). The same idea for the horizontal-only SDI of a shield: the x axis
 * has been out of its dead zone for at most one frame, the previous frame's x was below the SDI
 * minimum, and raw x moved more than 62 units over two frames. The reference compares the
 * previous x signed, not its magnitude, and so does this. */
int MeleeNativeUcfShieldSdiRule(int port, int x_held_frames, float prev_x, float min_magnitude) {
    if (!MeleeNativeUcfActive()) return 0;
    if (x_held_frames > 1) return 0;
    if (!(prev_x < min_magnitude)) return 0;
    return square(MeleeNativeUcfTravelX(port)) > square(SDI_TRAVEL);
}

/* 800908F4 (ftCo_DamageFall_IASA). Wiggling out of tumble needs the stick past a threshold with
 * the horizontal tilt timer below a window read from the fighter data (1 on the retail disc).
 * UCF replaces that comparison: timer 0 passes as before, and timer 1 passes when the previous
 * frame's |x| was still under the wiggle threshold and raw x moved more than 75 units over two
 * frames. The window from the fighter data is not consulted (the injection compares with the
 * constant 1), so `retail` is only returned while UCF is inactive. */
int MeleeNativeUcfTumbleRule(int retail, int port, int x_tilt_frames, float prev_x, float wiggle_threshold) {
    if (!MeleeNativeUcfActive()) return retail;
    if (x_tilt_frames != 1) return x_tilt_frames < 1;
    if (prev_x < 0.0f) prev_x = -prev_x;
    if (!(prev_x < wiggle_threshold)) return 0;
    return square(MeleeNativeUcfTravelX(port)) > square(TUMBLE_TRAVEL);
}

/* 800998A4 (entry of ftCo_80099894, reached from ftCo_80099794 and ftCo_8009980C). Holding
 * shield on a platform and rolling the stick down along the rim should drop through, but the
 * spot dodge is checked first and wins as soon as y passes its threshold. UCF cancels the spot
 * dodge (the caller then reports that nothing happened) when all of these hold: the C-stick is
 * not the one asking for it, the horizontal tilt is not fresh enough to be a roll input, stick y
 * is still above -0.8, the fighter stands on a drop-through floor, and the stick is on the rim. */
int MeleeNativeUcfSpotDodgeRule(float cstick_y, float dodge_threshold, int x_tilt_frames, int x_tilt_window,
                                float x, float y, int on_drop_through_floor) {
    if (!MeleeNativeUcfActive()) return 0;
    if (!(cstick_y > dodge_threshold)) return 0;
    if (x_tilt_frames < x_tilt_window) return 0;
    if (!(y > -0.8f)) return 0;
    if (!on_drop_through_floor) return 0;
    return MeleeNativeUcfOnRim(x, y);
}

/* 8009A0B8 (ftCo_8009A080). Dropping through a platform out of shield needs stick y at or below
 * a threshold. UCF also accepts a shield-drop flick that has been held for two frames or more
 * (MeleeNativeUcfTrackDrop); the game's other conditions still apply. */
int MeleeNativeUcfShieldDropRule(int port) {
    if (!MeleeNativeUcfActive()) return 0;
    return MeleeNativeUcfDropFrames(port) > 1;
}

#ifdef MELEE_NATIVE
/* The game side: which Fighter fields feed each rule. */
#include <melee/ft/fighter.h>
#include <melee/ft/forward.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <melee/ft/kinds/ftZelda/forward.h>
#include <melee/ft/types.h>
#include <melee/mp/forward.h>
#include <melee/mp/mpcoll.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>
#include <dolphin/pad.h>

/* The reference tests these two by number. */
_Static_assert(Ft_Kind_Zelda == 19, "UCF: Zelda's fighter kind moved");
_Static_assert(ftZd_MS_SpecialHiStart_0 == 349, "UCF: Zelda's up-special start state moved");

void MeleeNativeUcfFightersInit(void) {
    if (MeleeNativeUcfActive()) MeleeNativeUcfReset();
}

/* The raw sample this logic frame is simulated from. HSD_PadRenewMasterStatus takes one entry
 * off the raw queue per logic frame and steps the read index past it, so the entry just before
 * the read index is the one the pad status of this frame was built from (the reference reads the
 * same slot). Nothing polls the pads between that call and the fighters' input pass, and under
 * online play or a scripted run the queue already holds the agreed inputs. */
static const PADStatus* consumed_sample(int port) {
    const PadLibData* lib = &HSD_PadLibData;
    if (lib->queue == NULL || lib->qnum == 0) return NULL;
    return &lib->queue[(lib->qread + lib->qnum - 1) % lib->qnum].stat[port];
}

/* 8006B460: once per simulated frame for every fighter a pad controls (the reference asks
 * ftCo_IsCpuControlled, which also excludes Nana). Zelda is not snapped during the first
 * grounded up-special state, where the stick picks the direction of travel. The shield-drop
 * tracker sees the stick after snapping, as in the reference. A pad that reports an error reads
 * as centred, which is what the console's PADRead returns for it. */
void MeleeNativeUcfFighterInput(struct Fighter* fp) {
    const PADStatus* pad;
    int port, x = 0, y = 0, cx = 0, cy = 0;
    if (!MeleeNativeUcfActive() || ftCo_IsCpuControlled(fp)) return;
    port = fp->pad_port;
    if (!known_port(port)) return;
    pad = consumed_sample(port);
    if (pad == NULL) return;
    if (pad->err == 0) {
        x = pad->stickX;
        y = pad->stickY;
        cx = pad->substickX;
        cy = pad->substickY;
    }
    MeleeNativeUcfRecord(port, x, y);
    if (!(fp->kind == Ft_Kind_Zelda && fp->motion_id == ftZd_MS_SpecialHiStart_0)) {
        MeleeNativeUcfSnapCardinal(x, y, &fp->input.lstick[0].x, &fp->input.lstick[0].y);
        MeleeNativeUcfSnapCardinal(cx, cy, &fp->input.cstick[0].x, &fp->input.cstick[0].y);
    }
    MeleeNativeUcfTrackDrop(port, fp->input.lstick[0].x, fp->input.lstick[0].y, fp->active_timer.lstick.y);
}

/* 800C9A44: called right after the Turn state has flipped facing_dir for its input checks.
 * Marking the turn as done on this frame keeps the flipped facing and lets the dash check at the
 * end of ftCo_Turn_IASA start the dash. A partner fighter (the second Ice Climber) replays the
 * leader's inputs some frames later from a queue; the entry being written gets the new facing
 * and a full stick in that direction so the partner dashes back too. Sub-fighters themselves are
 * skipped. */
void MeleeNativeUcfTurn(struct Fighter* fp) {
    HSD_GObj* partner_gobj;
    if (fp->is_sub_fighter) return;
    if (!MeleeNativeUcfDashBackRule(fp->pad_port, fp->cur_anim_frame, fp->facing_dir * fp->input.lstick[0].x,
                                    p_ftCommonData->dash_smash_stick_threshold, fp->active_timer.lstick.x))
        return;
    fp->mv.co.turn.has_turned = true;
    fp->mv.co.turn.just_turned = true;
    partner_gobj = Player_GetEntityAtIndex(fp->player_idx, 1);
    if (partner_gobj != NULL) {
        Fighter* partner = partner_gobj->user_data;
        if (partner != NULL && partner->cpu.x444 != NULL) {
            partner->cpu.x444->facing_dir = fp->facing_dir;
            partner->cpu.x444->lstick.x = fp->facing_dir < 0.0f ? -128 : 127;
        }
    }
}

/* 800D65EC. */
float MeleeNativeUcfSquatRvThreshold(struct Fighter* fp, float retail) {
    return MeleeNativeUcfSquatRvRule(retail, fp->active_timer.lstick.x, fp->input.lstick[0].x,
                                     fp->input.lstick[0].y);
}

/* 8008E54C. */
int MeleeNativeUcfSdi(struct Fighter* fp) {
    return MeleeNativeUcfSdiRule(fp->pad_port, fp->active_sticky.lstick.x, fp->active_sticky.lstick.y,
                                 fp->input.lstick[1].x, fp->input.lstick[1].y, p_ftCommonData->sdi_min_stick_mag);
}

/* 80093294. */
int MeleeNativeUcfShieldSdi(struct Fighter* fp) {
    return MeleeNativeUcfShieldSdiRule(fp->pad_port, fp->active_sticky.lstick.x, fp->input.lstick[1].x,
                                       p_ftCommonData->sdi_min_stick_mag);
}

/* 800908F4. */
int MeleeNativeUcfTumble(struct Fighter* fp, int retail) {
    return MeleeNativeUcfTumbleRule(retail, fp->pad_port, fp->active_timer.lstick.x, fp->input.lstick[1].x,
                                    p_ftCommonData->x210);
}

/* 800998A4: x314 is the stick height that asks for a spot dodge and x320 the roll window. */
int MeleeNativeUcfBlocksSpotDodge(struct Fighter* fp) {
    return MeleeNativeUcfSpotDodgeRule(
        fp->input.cstick[0].y, p_ftCommonData->x314, fp->active_timer.lstick.x, p_ftCommonData->x320,
        fp->input.lstick[0].x, fp->input.lstick[0].y,
        fp->coll_data.floor.index != -1 && (fp->coll_data.floor.flags & LINE_FLAG_PLATFORM) != 0);
}

/* 8009A0B8: the flick stands in for the stick-height test of ftCo_80099F1C only; its tilt-timer
 * and platform conditions are repeated here unchanged. */
int MeleeNativeUcfShieldDrop(struct Fighter* fp) {
    if (!MeleeNativeUcfShieldDropRule(fp->pad_port)) return 0;
    return fp->active_timer.lstick.y < p_ftCommonData->x468 && mpColl_IsOnPlatform(&fp->coll_data);
}
#endif
