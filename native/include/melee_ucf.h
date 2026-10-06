#pragma once
/* Universal Controller Fix 0.84 as an opt-in input option (settings key "ucf", MELEE_UCF).
 *
 * UCF makes a handful of stick inputs depend on how far the raw stick travelled instead of on
 * which single raw value a worn or loose stick happened to report on one poll. Every rule and
 * threshold below was derived from the UCF 0.84 codes as injected into the retail game (the
 * GameCube address of each injection is given with its rule in native/ucf.c).
 *
 * The first half is plain arithmetic on ints and floats and has no game dependency
 * (native/tests/ucf_test.c). The second half reads and writes a Fighter and only exists in the
 * game build. With the setting off, or during an online session, every rule answers "no" and
 * every recorder does nothing, so the hooks in the game code reduce to the retail expression. */
#ifdef __cplusplus
extern "C" {
#endif

/* Controller ports with a stick history. Other port numbers are never recorded and read as an
 * unmoved stick. */
#define MELEE_UCF_PORTS 4

/* 1 when the fixes apply: the setting is on and no online session is configured (peers do not
 * exchange this setting, so an online game always runs the retail rules on both sides). */
int MeleeNativeUcfActive(void);

/* Forgets every port's stick history and shield-drop count. */
void MeleeNativeUcfReset(void);

/* Stick history: the raw main-stick bytes of the last four simulated frames of one port. */
void MeleeNativeUcfRecord(int port, int raw_x, int raw_y);
/* Newest raw sample minus the one recorded two frames before it (0 for an unknown port). */
int MeleeNativeUcfTravelX(int port);
int MeleeNativeUcfTravelY(int port);

/* 1 when a scaled stick position (1.0 = 80 raw units) lies on the outer rim of the gate. */
int MeleeNativeUcfOnRim(float x, float y);

/* Raw stick within 6 units of an axis and at least 80 along it: writes exactly +-1.0 on that
 * axis and 0 on the other and returns 1. Otherwise leaves *x and *y alone and returns 0. */
int MeleeNativeUcfSnapCardinal(int raw_x, int raw_y, float* x, float* y);

/* Shield-drop flick counter, updated once per simulated frame after MeleeNativeUcfRecord.
 * y_tilt_frames is the game's count of frames since the stick left the vertical dead zone. */
void MeleeNativeUcfTrackDrop(int port, float x, float y, int y_tilt_frames);
int MeleeNativeUcfDropFrames(int port);

/* The rules, on plain numbers. Each is documented at its definition. */
int MeleeNativeUcfDashBackRule(int port, float anim_frame, float stick_toward_facing, float dash_threshold,
                               int x_tilt_frames);
float MeleeNativeUcfSquatRvRule(float retail_threshold, int x_tilt_frames, float x, float y);
int MeleeNativeUcfSdiRule(int port, int x_held_frames, int y_held_frames, float prev_x, float prev_y,
                          float min_magnitude);
int MeleeNativeUcfShieldSdiRule(int port, int x_held_frames, float prev_x, float min_magnitude);
int MeleeNativeUcfTumbleRule(int retail, int port, int x_tilt_frames, float prev_x, float wiggle_threshold);
int MeleeNativeUcfSpotDodgeRule(float cstick_y, float dodge_threshold, int x_tilt_frames, int x_tilt_window,
                                float x, float y, int on_drop_through_floor);
int MeleeNativeUcfShieldDropRule(int port);

#ifdef MELEE_NATIVE
/* Game hooks. `retail` arguments are the value of the expression the hook replaces; it is
 * returned unchanged while UCF is inactive. */
struct Fighter;
/* fighter.c, Fighter_FirstInitialize_80067A84. */
void MeleeNativeUcfFightersInit(void);
/* fighter.c, Fighter_procInput: record, snap cardinals, track the shield-drop flick. */
void MeleeNativeUcfFighterInput(struct Fighter* fp);
/* ftCo_Turn.c, ftCo_Turn_IASA: turn the slow turnaround into a dash back. */
void MeleeNativeUcfTurn(struct Fighter* fp);
/* ftCo_SquatRv.c, ftCo_SquatRv_CheckInput: stick height that leaves the crouch. */
float MeleeNativeUcfSquatRvThreshold(struct Fighter* fp, float retail);
/* ftCo_Damage.c, ftCo_Damage_OnEveryHitlag: extra way to qualify for an SDI step. */
int MeleeNativeUcfSdi(struct Fighter* fp);
/* ftCo_Guard.c, ftCo_80093240: extra way to qualify for a shield SDI step. */
int MeleeNativeUcfShieldSdi(struct Fighter* fp);
/* ftCo_DamageFall.c, ftCo_DamageFall_IASA: the "stick was just flicked" half of the wiggle. */
int MeleeNativeUcfTumble(struct Fighter* fp, int retail);
/* ftCo_Escape.c, callers of ftCo_80099894: 1 when the spot dodge must not start. */
int MeleeNativeUcfBlocksSpotDodge(struct Fighter* fp);
/* ftCo_Pass.c, ftCo_8009A080: extra way to drop through a platform out of shield. */
int MeleeNativeUcfShieldDrop(struct Fighter* fp);
#endif

#ifdef __cplusplus
}
#endif
