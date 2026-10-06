/* The game-independent half of the controller fix (native/ucf.c): stick history and travel, the
 * rim test, cardinal snapping, the shield-drop counter, each rule on plain numbers, and that
 * nothing answers "yes" or records anything while the setting is off or a session is online. */
#include "melee_settings.h"
#include "melee_ucf.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); abort(); } } while (0)

/* Stands in for the online-play runtime (ucf.c refers to it weakly). */
static int online;
int MeleeNativeNetplayActive(void) { return online; }

/* A stick position the way the game holds it: raw units over 80. */
static float pos(int raw) { return (float) raw / 80.0f; }

/* Leaves the port with the given travel over two frames. */
static void move(int port, int dx, int dy) {
    MeleeNativeUcfRecord(port, 0, 0);
    MeleeNativeUcfRecord(port, dx / 2, dy / 2);
    MeleeNativeUcfRecord(port, dx, dy);
}

static int iabs(int v) { return v < 0 ? -v : v; }

/* Every rule with arguments that say "yes" when the fix is active. */
static int any_rule_fires(int port) {
    return MeleeNativeUcfDashBackRule(port, 2.0f, 1.0f, 0.8f, 1)
        || MeleeNativeUcfSquatRvRule(0.625f, 0, pos(60), pos(-53)) != 0.625f
        || MeleeNativeUcfSdiRule(port, 1, 1, 0.0f, 0.0f, 0.7f)
        || MeleeNativeUcfShieldSdiRule(port, 1, 0.0f, 0.7f)
        || MeleeNativeUcfTumbleRule(0, port, 1, 0.0f, 0.8f)
        || MeleeNativeUcfSpotDodgeRule(0.0f, -0.7f, 4, 4, pos(60), pos(-53), 1)
        || MeleeNativeUcfShieldDropRule(port);
}

static void test_off(void) {
    float x = 0.5f, y = 0.25f;
    MeleeNativeSettingsData.ucf = 0;
    online = 0;
    MeleeNativeUcfReset();
    CHECK(!MeleeNativeUcfActive());
    /* Nothing is recorded... */
    move(0, 127, -128);
    CHECK(MeleeNativeUcfTravelX(0) == 0 && MeleeNativeUcfTravelY(0) == 0);
    MeleeNativeUcfTrackDrop(0, 0.0f, -1.0f, 0);
    CHECK(MeleeNativeUcfDropFrames(0) == 0);
    /* ...and with a history that would satisfy every rule, none fires. */
    MeleeNativeSettingsData.ucf = 1;
    move(0, 100, -100);
    MeleeNativeUcfTrackDrop(0, 0.0f, -1.0f, 0);
    MeleeNativeUcfTrackDrop(0, 0.0f, -1.0f, 1);
    CHECK(MeleeNativeUcfDropFrames(0) == 2);
    CHECK(any_rule_fires(0));
    MeleeNativeSettingsData.ucf = 0;
    CHECK(!any_rule_fires(0));
    CHECK(MeleeNativeUcfDashBackRule(0, 2.0f, 1.0f, 0.8f, 1) == 0);
    CHECK(MeleeNativeUcfSquatRvRule(0.625f, 0, pos(60), pos(-53)) == 0.625f);
    CHECK(MeleeNativeUcfSdiRule(0, 1, 1, 0.0f, 0.0f, 0.7f) == 0);
    CHECK(MeleeNativeUcfShieldSdiRule(0, 1, 0.0f, 0.7f) == 0);
    CHECK(MeleeNativeUcfSpotDodgeRule(0.0f, -0.7f, 4, 4, pos(60), pos(-53), 1) == 0);
    CHECK(MeleeNativeUcfShieldDropRule(0) == 0);
    /* The two rules that replace a game expression hand it back untouched. */
    CHECK(MeleeNativeUcfTumbleRule(0, 0, 1, 0.0f, 0.8f) == 0);
    CHECK(MeleeNativeUcfTumbleRule(1, 0, 5, 0.9f, 0.8f) == 1);
    CHECK(MeleeNativeUcfSquatRvRule(0.3f, 0, 1.0f, 0.0f) == 0.3f);
    /* The recorders stay idle too. */
    MeleeNativeUcfRecord(0, 5, 5);
    MeleeNativeUcfTrackDrop(0, 0.0f, 0.0f, 0);
    CHECK(MeleeNativeUcfTravelX(0) == 100 && MeleeNativeUcfDropFrames(0) == 2);
    /* The geometry helpers are not rules: they answer regardless and change nothing by themselves. */
    CHECK(MeleeNativeUcfOnRim(1.0f, 0.0f));
    CHECK(MeleeNativeUcfSnapCardinal(1, 1, &x, &y) == 0 && x == 0.5f && y == 0.25f);

    /* An online session switches the fix off whatever the setting says. */
    MeleeNativeSettingsData.ucf = 1;
    CHECK(MeleeNativeUcfActive() && any_rule_fires(0));
    online = 1;
    CHECK(!MeleeNativeUcfActive() && !any_rule_fires(0));
    CHECK(MeleeNativeUcfTumbleRule(1, 0, 5, 0.9f, 0.8f) == 1);
    MeleeNativeUcfRecord(0, 5, 5);
    CHECK(MeleeNativeUcfTravelX(0) == 100);
    online = 0;
}

static void test_history(void) {
    MeleeNativeSettingsData.ucf = 1;
    MeleeNativeUcfReset();
    CHECK(MeleeNativeUcfTravelX(2) == 0 && MeleeNativeUcfTravelY(2) == 0);
    /* Travel is the newest sample minus the one two frames older. */
    MeleeNativeUcfRecord(2, 10, -3);
    CHECK(MeleeNativeUcfTravelX(2) == 10 && MeleeNativeUcfTravelY(2) == -3);
    MeleeNativeUcfRecord(2, 30, -20);
    CHECK(MeleeNativeUcfTravelX(2) == 30 && MeleeNativeUcfTravelY(2) == -20);
    MeleeNativeUcfRecord(2, 70, -50);
    CHECK(MeleeNativeUcfTravelX(2) == 60 && MeleeNativeUcfTravelY(2) == -47);
    MeleeNativeUcfRecord(2, 75, -90);
    CHECK(MeleeNativeUcfTravelX(2) == 45 && MeleeNativeUcfTravelY(2) == -70);
    /* The ring holds four samples; a long run keeps giving the right pair. */
    for (int i = 0; i < 40; ++i) {
        MeleeNativeUcfRecord(2, i, -2 * i);
        if (i >= 2) CHECK(MeleeNativeUcfTravelX(2) == 2 && MeleeNativeUcfTravelY(2) == -4);
    }
    /* A stick that stops moving reads as no travel after two frames. */
    MeleeNativeUcfRecord(2, 39, -78);
    CHECK(MeleeNativeUcfTravelX(2) == 1);
    MeleeNativeUcfRecord(2, 39, -78);
    CHECK(MeleeNativeUcfTravelX(2) == 0 && MeleeNativeUcfTravelY(2) == 0);
    /* Full-range bytes do not overflow. */
    MeleeNativeUcfRecord(2, -128, 127);
    MeleeNativeUcfRecord(2, 0, 0);
    MeleeNativeUcfRecord(2, 127, -128);
    CHECK(MeleeNativeUcfTravelX(2) == 255 && MeleeNativeUcfTravelY(2) == -255);
    /* Ports are independent, and port numbers without a pad have no history. */
    CHECK(MeleeNativeUcfTravelX(0) == 0 && MeleeNativeUcfTravelX(1) == 0 && MeleeNativeUcfTravelX(3) == 0);
    MeleeNativeUcfRecord(4, 100, 100);
    MeleeNativeUcfRecord(-1, 100, 100);
    MeleeNativeUcfRecord(255, 100, 100);
    CHECK(MeleeNativeUcfTravelX(4) == 0 && MeleeNativeUcfTravelX(-1) == 0 && MeleeNativeUcfTravelY(255) == 0);
    CHECK(MeleeNativeUcfDropFrames(4) == 0 && MeleeNativeUcfDropFrames(-1) == 0);
    CHECK(MeleeNativeUcfTravelX(2) == 255 && MeleeNativeUcfTravelX(3) == 0);
    MeleeNativeUcfReset();
    CHECK(MeleeNativeUcfTravelX(2) == 0 && MeleeNativeUcfTravelY(2) == 0);
}

static void test_rim(void) {
    /* Along an axis the rim starts at 79 of 80 units. */
    CHECK(MeleeNativeUcfOnRim(1.0f, 0.0f) && MeleeNativeUcfOnRim(-1.0f, 0.0f));
    CHECK(MeleeNativeUcfOnRim(0.0f, 1.0f) && MeleeNativeUcfOnRim(0.0f, -1.0f));
    CHECK(MeleeNativeUcfOnRim(pos(79), 0.0f) && !MeleeNativeUcfOnRim(pos(78), 0.0f));
    CHECK(MeleeNativeUcfOnRim(0.0f, pos(-79)) && !MeleeNativeUcfOnRim(0.0f, pos(-78)));
    /* On the diagonal, 56/56 is on it and 55/55 is not. */
    CHECK(MeleeNativeUcfOnRim(pos(56), pos(56)) && MeleeNativeUcfOnRim(pos(-56), pos(-56)));
    CHECK(!MeleeNativeUcfOnRim(pos(55), pos(55)) && !MeleeNativeUcfOnRim(pos(55), pos(-55)));
    /* A typical shield-drop notch position, and the same angle further in. */
    CHECK(MeleeNativeUcfOnRim(pos(59), pos(-53)) && MeleeNativeUcfOnRim(pos(-59), pos(-53)));
    CHECK(!MeleeNativeUcfOnRim(pos(50), pos(-45)));
    CHECK(!MeleeNativeUcfOnRim(0.0f, 0.0f) && !MeleeNativeUcfOnRim(0.5f, 0.5f));
    /* Every position a pad can report: a reading of k units counts as k + 1 (at least 2), and
     * the position is on the rim when that lies outside radius 80. Both ways the game or a
     * caller may form k / 80 agree. */
    for (int x = -80; x <= 80; ++x) {
        for (int y = -80; y <= 80; ++y) {
            int ux = (iabs(x) > 1 ? iabs(x) : 1) + 1, uy = (iabs(y) > 1 ? iabs(y) : 1) + 1;
            int expected = ux * ux + uy * uy > 6400;
            CHECK(MeleeNativeUcfOnRim(pos(x), pos(y)) == expected);
            CHECK(MeleeNativeUcfOnRim((float) x * 0.0125f, (float) y * 0.0125f) == expected);
        }
    }
}

static int snaps_to(int raw_x, int raw_y, float want_x, float want_y) {
    float x = 0.123f, y = -0.456f;
    return MeleeNativeUcfSnapCardinal(raw_x, raw_y, &x, &y) == 1 && x == want_x && y == want_y;
}

static int untouched(int raw_x, int raw_y) {
    float x = 0.123f, y = -0.456f;
    return MeleeNativeUcfSnapCardinal(raw_x, raw_y, &x, &y) == 0 && x == 0.123f && y == -0.456f;
}

static void test_snap(void) {
    /* 80 along the axis is the first value that snaps. */
    CHECK(snaps_to(80, 0, 1.0f, 0.0f) && untouched(79, 0));
    CHECK(snaps_to(-80, 0, -1.0f, 0.0f) && untouched(-79, 0));
    CHECK(snaps_to(0, 80, 0.0f, 1.0f) && untouched(0, 79));
    CHECK(snaps_to(0, -80, 0.0f, -1.0f) && untouched(0, -79));
    /* Up to 6 off the axis either way, not 7. */
    CHECK(snaps_to(80, 6, 1.0f, 0.0f) && snaps_to(80, -6, 1.0f, 0.0f));
    CHECK(untouched(80, 7) && untouched(80, -7));
    CHECK(snaps_to(-95, 3, -1.0f, 0.0f) && untouched(-95, -7));
    CHECK(snaps_to(6, -80, 0.0f, -1.0f) && snaps_to(-6, 80, 0.0f, 1.0f));
    CHECK(untouched(7, -80) && untouched(-7, 80));
    /* The ends of the byte range. */
    CHECK(snaps_to(127, 0, 1.0f, 0.0f) && snaps_to(-128, 0, -1.0f, 0.0f));
    CHECK(snaps_to(0, 127, 0.0f, 1.0f) && snaps_to(0, -128, 0.0f, -1.0f));
    /* A corner is past 80 on x with too much y, and is not retried as a vertical cardinal. */
    CHECK(untouched(80, 80) && untouched(-100, -100) && untouched(127, -128));
    CHECK(untouched(0, 0) && untouched(40, 40) && untouched(79, 6) && untouched(5, 79));
}

static void test_drop_counter(void) {
    MeleeNativeSettingsData.ucf = 1;
    MeleeNativeUcfReset();
    /* Resting, then a flick straight down that takes two frames to arrive. */
    for (int i = 0; i < 3; ++i) {
        MeleeNativeUcfRecord(1, 0, 0);
        MeleeNativeUcfTrackDrop(1, 0.0f, 0.0f, 254);
        CHECK(MeleeNativeUcfDropFrames(1) == 0);
    }
    MeleeNativeUcfRecord(1, 0, -40);
    MeleeNativeUcfTrackDrop(1, 0.0f, pos(-40), 0);
    CHECK(MeleeNativeUcfDropFrames(1) == 0 && !MeleeNativeUcfShieldDropRule(1));
    MeleeNativeUcfRecord(1, 0, -80);
    MeleeNativeUcfTrackDrop(1, 0.0f, -1.0f, 1);
    CHECK(MeleeNativeUcfDropFrames(1) == 1 && !MeleeNativeUcfShieldDropRule(1));
    /* Held: the count runs without any further movement or a fresh tilt timer... */
    MeleeNativeUcfRecord(1, 0, -80);
    MeleeNativeUcfTrackDrop(1, 0.0f, -1.0f, 2);
    CHECK(MeleeNativeUcfDropFrames(1) == 2 && MeleeNativeUcfShieldDropRule(1));
    MeleeNativeUcfRecord(1, 0, -80);
    MeleeNativeUcfTrackDrop(1, 0.0f, -1.0f, 3);
    CHECK(MeleeNativeUcfDropFrames(1) == 3 && MeleeNativeUcfShieldDropRule(1));
    CHECK(MeleeNativeUcfDropFrames(0) == 0 && !MeleeNativeUcfShieldDropRule(0));
    /* ...also along the rim, as long as y stays at or below -0.609375... */
    MeleeNativeUcfTrackDrop(1, pos(63), -0.609375f, 4);
    CHECK(MeleeNativeUcfDropFrames(1) == 4);
    /* ...and ends when the stick comes up past it or leaves the rim. */
    MeleeNativeUcfTrackDrop(1, pos(64), pos(-48), 5);
    CHECK(MeleeNativeUcfDropFrames(1) == 0 && !MeleeNativeUcfShieldDropRule(1));
    MeleeNativeUcfTrackDrop(1, 0.0f, -1.0f, 0);
    CHECK(MeleeNativeUcfDropFrames(1) == 0); /* the stick has not moved for two frames */
    move(1, 0, -80);
    MeleeNativeUcfTrackDrop(1, 0.0f, -1.0f, 0);
    CHECK(MeleeNativeUcfDropFrames(1) == 1);
    MeleeNativeUcfTrackDrop(1, 0.0f, pos(-70), 1);
    CHECK(MeleeNativeUcfDropFrames(1) == 0); /* 70 straight down is short of the rim */

    /* Starting needs more than 44 units of vertical travel, in either direction... */
    move(1, 0, -44);
    MeleeNativeUcfTrackDrop(1, pos(60), pos(-53), 0);
    CHECK(MeleeNativeUcfDropFrames(1) == 0);
    move(1, 0, -45);
    MeleeNativeUcfTrackDrop(1, pos(60), pos(-53), 0);
    CHECK(MeleeNativeUcfDropFrames(1) == 1);
    MeleeNativeUcfTrackDrop(1, 0.0f, 0.0f, 0);
    move(1, 0, 45);
    MeleeNativeUcfTrackDrop(1, pos(60), pos(-53), 1);
    CHECK(MeleeNativeUcfDropFrames(1) == 1);
    MeleeNativeUcfTrackDrop(1, 0.0f, 0.0f, 0);
    /* ...horizontal travel does not count... */
    move(1, 120, 0);
    MeleeNativeUcfTrackDrop(1, pos(60), pos(-53), 0);
    CHECK(MeleeNativeUcfDropFrames(1) == 0);
    /* ...and the vertical tilt timer must be 0 or 1 on that frame. */
    move(1, 0, -80);
    MeleeNativeUcfTrackDrop(1, 0.0f, -1.0f, 2);
    CHECK(MeleeNativeUcfDropFrames(1) == 0);
    MeleeNativeUcfTrackDrop(1, 0.0f, -1.0f, 1);
    CHECK(MeleeNativeUcfDropFrames(1) == 1);

    /* One byte, like the reference: 255 frames of holding wrap to 0, where it then stays
     * because the stick is no longer moving. */
    for (int i = 2; i <= 255; ++i) {
        MeleeNativeUcfRecord(1, 0, -80);
        MeleeNativeUcfTrackDrop(1, 0.0f, -1.0f, i < 254 ? i : 254);
        CHECK(MeleeNativeUcfDropFrames(1) == i);
    }
    MeleeNativeUcfRecord(1, 0, -80);
    MeleeNativeUcfTrackDrop(1, 0.0f, -1.0f, 254);
    CHECK(MeleeNativeUcfDropFrames(1) == 0);
    MeleeNativeUcfTrackDrop(1, 0.0f, -1.0f, 254);
    CHECK(MeleeNativeUcfDropFrames(1) == 0);

    /* A port without a pad never counts. */
    MeleeNativeUcfTrackDrop(4, 0.0f, -1.0f, 0);
    CHECK(MeleeNativeUcfDropFrames(4) == 0 && !MeleeNativeUcfShieldDropRule(4));
}

static void test_rules(void) {
    MeleeNativeSettingsData.ucf = 1;
    MeleeNativeUcfReset();

    /* Dash back: frame 2 of the turn, stick at the threshold or past it, tilt timer 0 or 1,
     * more than 75 units of horizontal travel (either direction). */
    move(0, -76, 0);
    CHECK(MeleeNativeUcfDashBackRule(0, 2.0f, 0.9f, 0.8f, 1));
    CHECK(MeleeNativeUcfDashBackRule(0, 2.0f, 0.8f, 0.8f, 0));
    CHECK(!MeleeNativeUcfDashBackRule(0, 1.0f, 0.9f, 0.8f, 1));
    CHECK(!MeleeNativeUcfDashBackRule(0, 3.0f, 0.9f, 0.8f, 1));
    CHECK(!MeleeNativeUcfDashBackRule(0, 2.0f, 0.79f, 0.8f, 1));
    CHECK(!MeleeNativeUcfDashBackRule(0, 2.0f, -0.9f, 0.8f, 1));
    CHECK(!MeleeNativeUcfDashBackRule(0, 2.0f, 0.9f, 0.8f, 2));
    CHECK(!MeleeNativeUcfDashBackRule(1, 2.0f, 0.9f, 0.8f, 1));
    CHECK(!MeleeNativeUcfDashBackRule(7, 2.0f, 0.9f, 0.8f, 1));
    move(0, 75, 100);
    CHECK(!MeleeNativeUcfDashBackRule(0, 2.0f, 0.9f, 0.8f, 1));
    move(0, 76, 0);
    CHECK(MeleeNativeUcfDashBackRule(0, 2.0f, 0.9f, 0.8f, 1));

    /* Crouch exit threshold: 0.59 only on the first frame of horizontal tilt on the rim. */
    CHECK(MeleeNativeUcfSquatRvRule(0.625f, 0, pos(60), pos(-53)) == 0.59f);
    CHECK(MeleeNativeUcfSquatRvRule(0.625f, 1, pos(60), pos(-53)) == 0.625f);
    CHECK(MeleeNativeUcfSquatRvRule(0.625f, 0, pos(50), pos(-45)) == 0.625f);

    /* SDI: one axis fresh, previous stick inside the minimum, more than 62 units travelled. */
    move(0, 40, -50); /* 64.03 units */
    CHECK(MeleeNativeUcfSdiRule(0, 1, 1, 0.5f, 0.4f, 0.7f));
    CHECK(MeleeNativeUcfSdiRule(0, 200, 1, 0.5f, 0.4f, 0.7f));
    CHECK(MeleeNativeUcfSdiRule(0, 0, 200, 0.5f, 0.4f, 0.7f));
    CHECK(!MeleeNativeUcfSdiRule(0, 2, 2, 0.5f, 0.4f, 0.7f));
    CHECK(!MeleeNativeUcfSdiRule(0, 1, 1, 0.7f, 0.0f, 0.7f));
    CHECK(!MeleeNativeUcfSdiRule(0, 1, 1, 0.5f, 0.5f, 0.7f));
    CHECK(MeleeNativeUcfSdiRule(0, 1, 1, pos(55), 0.0f, 0.7f));
    CHECK(!MeleeNativeUcfSdiRule(3, 1, 1, 0.5f, 0.4f, 0.7f));
    move(0, 62, 0);
    CHECK(!MeleeNativeUcfSdiRule(0, 1, 1, 0.0f, 0.0f, 0.7f));
    move(0, 0, 63);
    CHECK(MeleeNativeUcfSdiRule(0, 1, 1, 0.0f, 0.0f, 0.7f));

    /* Shield SDI: x only; the previous x is compared signed. */
    move(0, 63, 0);
    CHECK(MeleeNativeUcfShieldSdiRule(0, 1, 0.3f, 0.7f));
    CHECK(MeleeNativeUcfShieldSdiRule(0, 0, -0.9f, 0.7f));
    CHECK(!MeleeNativeUcfShieldSdiRule(0, 2, 0.3f, 0.7f));
    CHECK(!MeleeNativeUcfShieldSdiRule(0, 1, 0.7f, 0.7f));
    move(0, 62, 100);
    CHECK(!MeleeNativeUcfShieldSdiRule(0, 1, 0.3f, 0.7f));

    /* Tumble: timer 0 always passes and timer 2 or more never does, whatever the game's own
     * comparison said; timer 1 passes on a flick from inside the wiggle threshold. */
    move(0, -76, 0);
    CHECK(MeleeNativeUcfTumbleRule(0, 0, 0, 0.9f, 0.8f) == 1);
    CHECK(MeleeNativeUcfTumbleRule(1, 0, 2, 0.0f, 0.8f) == 0);
    CHECK(MeleeNativeUcfTumbleRule(0, 0, 1, 0.5f, 0.8f) == 1);
    CHECK(MeleeNativeUcfTumbleRule(0, 0, 1, -0.5f, 0.8f) == 1);
    CHECK(MeleeNativeUcfTumbleRule(1, 0, 1, 0.8f, 0.8f) == 0);
    CHECK(MeleeNativeUcfTumbleRule(1, 0, 1, -0.9f, 0.8f) == 0);
    CHECK(MeleeNativeUcfTumbleRule(0, 2, 1, 0.5f, 0.8f) == 0);
    move(0, 75, 0);
    CHECK(MeleeNativeUcfTumbleRule(1, 0, 1, 0.5f, 0.8f) == 0);

    /* Spot dodge is cancelled only when every condition holds. */
    CHECK(MeleeNativeUcfSpotDodgeRule(0.0f, -0.7f, 4, 4, pos(60), pos(-53), 1));
    CHECK(MeleeNativeUcfSpotDodgeRule(0.0f, -0.7f, 254, 4, pos(48), pos(-63), 1));
    CHECK(!MeleeNativeUcfSpotDodgeRule(-0.7f, -0.7f, 4, 4, pos(60), pos(-53), 1)); /* C-stick dodge */
    CHECK(!MeleeNativeUcfSpotDodgeRule(-1.0f, -0.7f, 4, 4, pos(60), pos(-53), 1));
    CHECK(!MeleeNativeUcfSpotDodgeRule(0.0f, -0.7f, 3, 4, pos(60), pos(-53), 1));  /* roll window */
    CHECK(!MeleeNativeUcfSpotDodgeRule(0.0f, -0.7f, 4, 4, pos(48), pos(-64), 1));  /* y at -0.8 */
    CHECK(!MeleeNativeUcfSpotDodgeRule(0.0f, -0.7f, 4, 4, pos(0), pos(-80), 1));
    CHECK(!MeleeNativeUcfSpotDodgeRule(0.0f, -0.7f, 4, 4, pos(60), pos(-53), 0));  /* solid floor */
    CHECK(!MeleeNativeUcfSpotDodgeRule(0.0f, -0.7f, 4, 4, pos(50), pos(-57), 1));  /* off the rim */

    /* Shield drop: the flick must have been held for two frames. */
    MeleeNativeUcfReset();
    CHECK(!MeleeNativeUcfShieldDropRule(0));
    move(0, 0, -80);
    MeleeNativeUcfTrackDrop(0, 0.0f, -1.0f, 0);
    CHECK(!MeleeNativeUcfShieldDropRule(0));
    MeleeNativeUcfTrackDrop(0, 0.0f, -1.0f, 1);
    CHECK(MeleeNativeUcfShieldDropRule(0));
    MeleeNativeUcfReset();
    CHECK(!MeleeNativeUcfShieldDropRule(0) && MeleeNativeUcfDropFrames(0) == 0);
}

int main(void) {
    test_off();
    test_history();
    test_rim();
    test_snap();
    test_drop_counter();
    test_rules();
    puts("ucf ok");
    return 0;
}
