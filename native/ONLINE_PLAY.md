# Online play between two native builds (2026-09-19)

Native-to-native only: two devices running the same release of this port play one match over the
local network in fixed-delay lockstep. Nothing here talks to Slippi or to Dolphin; see
`FEATURE_FEASIBILITY.md` §6b for why cross-play with the PowerPC game is out of reach.

## How it works

- **Lockstep by pad poll** (`native/netplay_session.{h,cpp}`). Every `PADRead` is one poll. The local
  pad sampled at poll *p* is scheduled for poll *p + delay* and sent to the peer; poll *p* completes
  only once the peer's pad for *p* has arrived. Both games therefore apply identical four-port input
  at identical polls. Polls 0..delay-1 are neutral on both sides. Each UDP datagram repeats the last
  eight scheduled inputs, so a lost datagram costs nothing unless several vanish in a row; a poll that
  has to wait re-sends every 30 ms and gives up after `MELEE_ONLINE_STALL_TIMEOUT_MS` (10 s), after
  which the game continues offline with the local pad and says so on screen.
- **Desync detection.** `MeleeNativeStateHash()` (`native/matrix_runtime.c`) digests the RNG seed
  and every fighter's position, velocity, facing, damage, motion and stock count once per logic
  frame; the latest digest rides in every input packet and is compared against the peer's for the
  same frame. The first mismatch is logged (`netplay: DESYNC at frame N: local X remote Y`), shown in
  red on screen and counted in `[netplay-stats]`; the match keeps running.
- **Determinism prerequisites** the session turns on:
  - the boot RNG seed comes from `MELEE_TEST_SEED` (the host's random seed) instead of `OSGetTick`;
  - **deterministic I/O** (`MeleeNativeDeterministicIO()`, also `MELEE_DETERMINISTIC_IO=1`): disc
    reads run to completion on the game thread and ARAM copies happen immediately, and their
    callbacks fire when the game thread next re-enables interrupts or at the VI pump, never from a
    worker thread, so every load takes the same number of frames on both devices regardless of SD
    speed (Melee's loaders spin on state those callbacks set while toggling interrupts, which is why
    `OSRestoreInterrupts` is the delivery point; a retrace-only pump deadlocked at boot);
  - the intro movie (streamed on wall-clock time) is left on its first frame as if Start were
    pressed (`src/melee/gm/gmopening.c`);
  - the guest plays on a copy of the host's save (`AURORA_CARD_PATH_A`, Aurora 8863e23), so menus,
    unlocks and the memory-card prompt agree.
- **Discovery and handshake** (`native/netplay.cpp`, `Lobby`). From Options > Port Settings >
  Online Play: *Host a match* announces the device with a UDP beacon (`MLNP1 <build> <port> <name>`
  to every broadcast address, port 26261) and listens on TCP 26262; joiners list the hosts they hear
  (name and address, two most recent) and *Join* connects, both exchange `Hello` (protocol version,
  32-bit digest of the running binary, device name), the host sends the seed, its input-delay
  setting, the guest's port and its save files, the guest acknowledges, and both relaunch the game
  (`OSResetSystem`) with `MELEE_ONLINE_ROLE/PEER/PORT/DELAY`, `MELEE_TEST_SEED` and (guest)
  `AURORA_CARD_PATH_A` in the environment. The relaunched games open UDP 26263 (host) / 26264
  (guest), wait up to `MELEE_ONLINE_READY_TIMEOUT_MS` (90 s) for the first packet from the peer and
  run lockstep from the first poll, i.e. from the memory-card prompt through the menus into the match.
  A reset requested by the game itself clears the variables, so the next boot is offline.
- **On screen**: one line along the top (player, delay, ping, stalls), red for a lost connection or a
  desync. **In log.txt**: `[netplay] ...` events and a `[netplay-stats]` line every 5 s.
- **The character select screen is the lobby.** Press **Z** on the VS-mode character select screen:
  an overlay lists *Host a match*, one *Join name address* row per host heard on the LAN, the input
  delay (left/right; *Auto* picks it from the handshake's round trip: one-way latency in frames plus
  one, 1..8) and *Close*; A selects, B closes. After the handshake both games relaunch and an
  **autopilot** (the host's pad, scripted by scene: Start on the title, VS Mode, Melee) brings both to
  the character select screen within a few seconds, where each player picks a fighter with their own
  pad and Start leads to stage select and the match, exactly as with two local pads. The Options >
  Port Settings > Online Play page remains as the alternative entry and for the delay setting.

## Environment (also usable without the menu, e.g. from the matrix)

| Variable | Meaning |
| --- | --- |
| `MELEE_ONLINE_ROLE` | `host` or `join`; its presence enables the session |
| `MELEE_ONLINE_PEER` | the other device's IPv4 address |
| `MELEE_ONLINE_PORT` | GameCube port this side plays on: 0 (host) or 1 (guest) |
| `MELEE_ONLINE_DELAY` | input delay in polls (frames), 1..15; the host's Online Play setting |
| `MELEE_TEST_SEED` | boot RNG seed, identical on both sides |
| `MELEE_ONLINE_UDP_LOCAL` / `_PEER` | UDP ports (defaults 26263/26264 by role; two games on one host use these) |
| `MELEE_ONLINE_READY_TIMEOUT_MS`, `MELEE_ONLINE_STALL_TIMEOUT_MS` | 90000 and 10000 |
| `MELEE_STATE_HASH_LOG=N` | print `[state-hash] frame= hash= seed=` every N logic frames |
| `MELEE_ONLINE_TEST_LOSS=P` | tests: drop P percent of outgoing input datagrams (keepalives kept) |
| `MELEE_TRACE_PADS=lo-hi`, `MELEE_TRACE_RAND=lo-hi`, `MELEE_SCRIPT_PADS_ONLY=1` | diagnosis: pads applied per poll, RNG callers per frame, ignore physical pads |
| `MELEE_INPUT_SCRIPT_ONLINE=path` | test harness: script the relaunched session runs |
| `MELEE_DETERMINISTIC_IO=1` | deterministic disc/ARAM completion without a session (the determinism test) |

## Tests

- `native/tests/netplay_test.cpp` (`native_netplay_test`): two sessions on loopback; identical
  four-port input on both sides with the delay applied, desync detection at the chosen frame, an
  absent peer, a peer that vanishes mid-session.
- `build/matrix/determinism-test.sh <prefix>`: the same binary, seed, save and input script on the
  Flip and the RG351P with deterministic I/O; compares `[state-hash]` lines frame by frame.
- `build/matrix/online-test.sh <prefix> [delay] [seed]`: the Flip hosts, the RG351P joins, both run
  the scripted match; reports the `[netplay]` events, the stats line and the hash comparison.

## Determinism hazards found on the two devices (Flip ROCKNIX vs RG351P AmberELEC), in order

1. **Retrace-only completion delivery deadlocked boot.** Melee's SFX loader chains a disc read into
   an ARAM copy into the next read purely through completion callbacks while the boot code spins
   without reaching a retrace (`HSD_SynthSFXWaitForLoadCompletion`). Completions now fire when the
   game thread enables or disables interrupts, draining chains in one go.
2. **The intro movie ends on a wall-clock frame.** Both sides agreed through frame 60 and diverged at
   the frame the movie left (49 vs 53 random draws by the title's first frame, from different scene
   timing). The movie is skipped on its first frame in sessions and in the test mode.
3. **The AX audio callback draws random numbers on the audio thread.** `lbAudioAx`'s sound-effect
   setup calls `HSD_Randi` from the SDL audio thread at the audio clock's pace, so the game seed
   advanced by a device-dependent count (49 vs 53 draws in one frame). Off the game thread,
   `HSD_Rand` now uses its own seed (`random.c`, `MeleeNativeOnGameThread()`).
4. **The input script counted retraces, not polls.** A slow device runs several pad polls per
   presented frame, so a retrace-timed script pressed Start on different logic frames. The script
   now steps once per `PADRead`, which is also how online inputs are applied.
5. **The audio engine's state was advanced by the audio thread.** Even with the RNG split, the
   title's first frame drew 44, 46, 49 or 53 random numbers from run to run: game-thread code reads
   engine state (voice end flags, callbacks) that the mixer changes at the audio clock's pace. In
   deterministic mode the AX frame callback and the mixer run on the game thread, ten 5 ms frames
   per three logic frames (`MeleeNativeAudioTick` from the per-frame hook), and the audio thread only
   drains the mixed blocks (`audio_host.cpp`); below 60 Hz the output has gaps.
6. **Port settings that change code paths.** The Flip had *Debug Overlays* on and the RG off. That
   was not what moved the counts (see 7), but `debug_level` does change gameplay, so the handshake
   carries the host's `debug_overlays` and `debug_level`, both sides apply them for the session through
   the `MELEE_DEBUG_*` environment overrides, and the tests force them.
7. **The title screen stirs the seed by the wall clock.** `gmTitle_801A165C` reads the time of day
   and calls `HSD_Rand()` once per second of the current minute (the tracer showed 18 vs 64 draws in
   two runs on the same Flip, all from that loop and the demo setup it feeds). Skipped in
   deterministic mode; the session seed is random already. `MELEE_TRACE_RAND=lo-hi` (random.c) and
   `build/matrix/rand-trace.sh` symbolize every gameplay-RNG draw in a frame range for the next hunt.
8. **Pad polls lost in the queue during scene transitions.** With 7 fixed, both devices agreed for
   339 frames (title and main menu) and drifted three polls at the menu-to-CSS transition. Retraces
   during a transition poll the pads while nobody consumes HSD's five-entry queue; the extra polls
   are merged into the last entry, and how many depends on the device's GPU. In deterministic mode
   the retrace handler (`lb_0195.c`) skips the poll while the queue is full, so every poll is exactly
   one logic frame on every device. This one would have desynced real sessions too, since the
   lockstep is keyed by poll index.
9. **Several queued polls drained per loop iteration, and the queue flushed at scene entry.** With
   8 fixed the script pressed its buttons on the same frames, but the CSS still opened one frame
   apart: the scene loop checks for a scene change once per iteration and drains every queued poll
   as a frame in between, and the next scene starts by discarding all but the newest queued poll;
   both counts depend on how many polls piled up during the transition. In deterministic mode the
   scene-entry flush is skipped (`gmscene.c`), so the same polls give the same frames in the same
   scenes. (An earlier fix also clamped the loop to one poll per iteration; that clamp was removed
   once the simulation was made independent of the render count - see "How online play catches up"
   below - so a device can now drain several queued polls between renders and skip frames online.)

10. **The CFW's libm.** The first online sessions connected, agreed through the menus and into the
    match, then desynced at frame 647 with the RNG seeds still identical on both sides and the same
    two hashes in every rerun, even with the physical pads neutralized: the fighter physics computed
    a different last bit. The game imports `sinf`, `cosf`, `atan2f`, `sqrtf`, `atanf`, `acosf`,
    `asinf`, `tanf`, `powf`, `expf`, `fmodf` from the device's libm (hundreds of call sites), and
    ROCKNIX ships glibc 2.41 while AmberELEC ships 2.38; glibc 2.41 moved several binary32 functions to
    new correctly rounded implementations. The device build now links the build sysroot's `libm.a`
    (glibc 2.30) as a whole archive and drops `libm.so.6` from NEEDED (`--as-needed`), so every device
    runs identical math; `check_sdl_backends.sh --shim` and the built-zip test fail if the binary ever
    requires libm.so.6 or imports those functions again. The two-device determinism test had not
    caught this because its idle scripted match happened not to hit a differing input in 80 s.
## Toward online catch-up (frame skipping) - in progress

Goal: let a slow peer draw fewer frames than it simulates and still stay in lock-step, so an online
match runs at real time on both sides instead of at the slower device's frame rate (single-player
already does this via `native/vi_pacing.h`). That needs the simulation to be a pure function of the
frames simulated, independent of how often the picture is drawn. Two of Melee's draw-to-sim couplings
are now fixed; a third remains, so **online still holds to one render per logic frame for now**
(catch-up gated off when `MeleeNativeDeterministicIO()`, `vi_runtime.cpp`, and the `gmscene.c`
one-poll-per-iteration clamp kept).

Fixed:

1. **The particle list.** `particleSort` (`psdisp.c`) re-links the global particle list
   `hsd_804D0908[]` in place by blend kind, and the per-frame particle update walks that same list, so
   its order feeds back into the simulation. It used to run from the draw callback
   (`efLib_render_callback`), a render-count-dependent number of times; it now runs once per simulated
   frame from `MeleeNativeMatrixTick` right after the particle update (deterministic mode), and the
   render skips its `psFrameNum` advance so the draw-time sort finds nothing to do. The list state is
   identical at both points (nothing touches it between the update and the render), so single-frame
   visuals are unchanged. This was the cause of the frame ~2620 desync: with the clamp removed, the
   two peers sorted the list a different number of times.

2. **Draw-time RNG.** A gobj draw callback that pulled from the shared game seed would advance it a
   render-count-dependent number of times. No in-match render callback does today (surveyed: fighters,
   items, HUD, effects and stages draw no RNG from their `render_cb`; the `ifstatus.c` damage-number
   shake and the rest are all update procs), but as a guard, while a render callback is on the stack
   `HSD_Rand`/`HSD_Randf` draw from a separate visual seed (`random.c`, bracketed in `gmscene.c`)
   whose values are cosmetic and never feed back.

With those two fixed, a **quiet match is fully cadence-independent**: single-device A/B on the RG351P
(`match_nomovie`, `MELEE_DETERMINISTIC_IO`, every frame hashed), catch-up off (1 draw per frame) vs
on (the RG drew ~15 FPS while simulating ~30), was **byte-identical for all 6,909 common frames**
(before the particle fix it parted at 2620). A short online fight with items also stayed in sync for
all 948 hashed frames at different frame rates on the two devices.

Still open - the reason catch-up is not yet enabled online:

3. **A busy-fight coupling.** An *active* fight (the `online_p1`/`online_p2` scripts, which attack,
   grab and use specials, unlike `match_nomovie` where P1 stands still) draws a **render-cadence-
   dependent number of game-seed randoms** - some hit/effect path reads state computed during the
   draw. Evidence: the shipped clamped build (654bd5aa, true 1:1) is deterministic on that fight
   across repeated runs, but with the clamp removed a single-device catch-up-off-vs-on A/B parts at
   ~frame 2298, and the exact frame wanders with timing (the frame-skip pattern is wall-clock
   dependent). Same class as the particle bug, different consumer, not yet located. Localizing tools
   added this pass: a per-frame game-seed draw counter (`melee_native_game_rand_draws`, in the
   `[state-hash]` line as `draws=`) and `MELEE_FORCE_1_1` (forces 1:1 as a stable reference cadence).
   Finding and fixing this - and auditing any other hit/effect read of `jobj->mtx` set up at draw - is
   what remains before online catch-up can be turned on.

- **on10 (2026-09-19 evening, build 654bd5aa):** the regression baseline, 7,122 frames of an active
  item fight in sync with catch-up gated off and the clamp kept - the behaviour online still ships.

## Results

- **Determinism (2026-09-19, dt12):** Flip ROCKNIX (libmali g29p1, 640x480, ~50 FPS) and RG351P
  AmberELEC (libmali r13p0, 480x320, ~20 FPS), same binary, seed 777, the Flip's save on both,
  `match_nomovie` script: **all 5,360 common logic frames hash identical**, from boot through title,
  main menu, character select, stage select and about 80 s of the Fox vs Captain Falcon match on
  Battlefield (the RG's run ended at the matrix timeout; the Flip continued to frame 6,241). Scene
  transitions land on the same frame on both (title 3, menu 100, CSS 345, SSS 473, match 474).
- **First online session (on1, delay 3, Flip host / RG guest over Wi-Fi):** connected on the first
  poll, identical hashes and no desync through title, menu, CSS and SSS into the match; round trip
  21-119 ms; the guest (the slower device) stalled on 259 of 489 polls for 6.6 s in total. Both sides
  then declared the connection lost at the stage load: the RG spends more than the 10 s stall timeout
  loading Battlefield without polling, so the Flip gave up waiting for its input. Fix: a keepalive
  thread (four packets a second, independent of the game thread) and a timeout on *silence* rather
  than on the missing input, so a loading peer keeps the session while a vanished one still ends it.
- **on2/on3 (keepalives):** the session survived the RG's 65-74 s stage load and both fighters
  entered the match; desync at frame 647 on both runs with identical seeds and byte-identical pads
  (hazard 10, the CFW's libm).
- **on4 (static libm, delay 3):** connected on the first poll, **no desync over 4,049 common frames**
  (menus, CSS, SSS and about a minute of the match), connection never lost. Round trip 113-523 ms on
  this Wi-Fi; the Flip (host, faster) stalled on 1,205 of 3,964 polls waiting for the RG, 144.8 s in
  total of which 73.9 s was the RG's stage load; the RG stalled on 400 of 4,058 polls (20.5 s, worst
  4.2 s). The match runs at the slower device's pace (about 20 FPS here), as lockstep must.
- **on5 (both players fighting, different scripts per side, delay 3):** `online_p1` on the Flip and
  `online_p2` on the RG (walks, jumps, attacks, shield, grab, pause and unpause), **no desync over
  6,068 common frames** with 5,659 distinct states, both scripts ran to their last line, connection
  never lost. Flip stalls 1,897 (161 s, of which 62.7 s the RG's stage load), RG stalls 417 (12.3 s,
  worst 1.7 s), round trip 96-191 ms.
- **disc4 (end to end through the menu, `discovery-test.sh`):** both devices booted normally with no
  session in the environment; the Flip's script opened Options > Port Settings > Online Play > *Host a
  match*, the RG's script opened the same page, listed "Join flip-rocknix 10.0.0.179" from the beacon
  and joined; the handshake carried seed 1476242686, delay 2 (the host's setting), the debug settings
  and one save file; the guest logged `Card A path from the environment: .../USA/Card A.online`; both
  relaunched, connected on the first poll and played through title, menu, CSS, SSS into the match with
  all 153 sampled hashes identical and no desync. The guest's "connection lost" at the very end is the
  test harness killing the host at its time limit.
- **on6 (active fight with 20% of input datagrams dropped on both sides, `MELEE_ONLINE_TEST_LOSS=20`):**
  no desync over 175 sampled frames, connection never lost, both scripts to their last line; the
  redundancy (last eight inputs in every datagram, resend every 30 ms while waiting) absorbed the loss
  with the RG's worst stall at 572 ms and 453 stalls over 5,252 polls, no worse than the lossless run.
- **disc5 (the character select screen as lobby, `CSS=1 discovery-test.sh`):** both scripts pressed Z
  on the CSS; the Flip hosted, the RG listed "Join flip-rocknix 10.0.0.179" and joined; the handshake
  measured a 7 ms round trip and chose delay 2 (Auto); both relaunched, the autopilot reached the CSS
  on both within about 260 polls, the session script pressed Start, and the match ran with all 166
  sampled hashes identical and no desync.
