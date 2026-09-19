# Luxury-feature feasibility for the native Melee port

Written 2026-09-18 against `portmaster` 05b10d247 + Aurora 2d943c9, after the PortMaster
package (binary 6115ef8c) passed the 13-mode matrix on the RG351P and the Miyoo Flip.
Line numbers refer to that revision. Effort estimates assume one engineer who already knows
this tree and can test on the two devices; they cover implementation and device testing, not
community polish.

## 0. What the current port gives you to build on

These facts set the price of every item below.

| Fact | Where | Consequence |
| --- | --- | --- |
| The whole game is C. No asm, no stubs in `mn/`, `gm/`, `db/`, `if/`. | `src/melee/**` | Any gameplay or menu change is an ordinary C edit. |
| Retail disc is read live and read-only through nod. No extracted assets, no override directory. | `native/runtime_main.cpp:57-70`, Aurora `lib/dolphin/dvd/dvd.cpp` | New textures or jobjs cannot be injected into disc archives. Text-based UI is the only new UI. |
| Aurora already ships a texture-replacement subsystem and a DVD file-overlay API, both unwired. | Aurora `lib/gfx/texture_replacement.cpp`, `include/aurora/dvd.h:26-116` | HD textures, music replacement and file mods are integration work, not engine work. |
| The runtime has no config-file parser. Every knob is an environment variable set by `Melee.sh`. | `native/runtime_main.cpp:138-171`, `native/FLIP_HANDOFF.md:78-115` | The first user-facing option forces a small settings-file layer (or reuses `console-settings.txt`, `native/os_runtime.c:159-184`). |
| The game's own developer text system (DevText) and developer menu widget are compiled in and initialised every scene. | `src/melee/if/textdraw.c:301-323`, `src/melee/if/textlib_1.c:588-609`, created at `src/melee/gm/gmscene.c:242` | Free on-screen text overlays and a data-driven text menu, zero asset work. |
| ImGui is linked and already drawn to once per frame. | Aurora `cmake/aurora_core.cmake:44-45`, `native/vi_runtime.cpp:79-80` | A second overlay path, but it renders through Dawn and re-adds a pass that `MELEE_FLIP_SKIP_EMPTY_IMGUI` currently elides. Prefer DevText on the handhelds. |
| Game memory is one contiguous MEM1 block (`mem1Size`, arena from `+0x4000`); game globals live in the ELF `.data/.bss`. | Aurora `lib/dolphin/os/OSMemory.cpp:31-32`, `OSArena.cpp:51-58` | Save states need MEM1 plus the binary's data segment plus host-side state, not just a heap copy. |
| Frame loop is wall-clock paced at 16.667 ms with catch-up; logic frames per presented frame vary. | `native/vi_runtime.cpp:38, 122-137` | Same-binary input replay is deterministic per logic frame; cross-platform (Dolphin) parity is not. |
| Free RAM in play: RG351P 60-140 MB, Flip ~110 MB+ on Panfrost, more on libmali. | matrix panguard logs | Anything that buffers (HD textures, replay buffers, save states) must be sized per device. |
| Only 4 pads, no keyboard rebinding, Aurora per-controller `.controller` profiles exist but have no UI. | Aurora `lib/dolphin/pad/pad.cpp:520-620, 1457-1490`, `lib/input.cpp:56-61` | Controller profiles are a UI problem, not a storage problem. |

## 1. Debug mode (the built-in DEBUG MENU and in-fight overlays)

**Verdict: feasible, cheap, and all assets are on the retail disc.** `DbCo.dat` and `SmSt.dat`
(motion-state names, sound test data) are both in the US 1.02 FST (checked against the disc
in `~/Desktop/DecompWorkspace`).

How it is gated today:

- `DbLevel` (`src/melee/db/db.h:9-15`: Master 0, NoDebugRom 1, DebugDevelop 2, DebugRom 3,
  Develop 4) is set once at boot in `src/melee/gm/gmmain.c:60-104`. Without `/develop.ini` on
  the disc it is forced to Master. With the file, X or Y held at power-on selects the level.
- Everything interesting needs `DbLevel >= 3`: title-screen exits to the debug menu
  (`gmtitlemode.c:40-58`, Y), `db_Setup` registering the overlays (`dbinit.c:72-96`), debug
  pause and frame step (`gmscene.c:318-319`), the developer options menu (`soundtest.c:2361`).

What you get for free at level 3, all decompiled and native-safe (DevText already has its
native `vsnprintf` fix at `textlib.c:253-265`):

- Hitbox, hurtbox, shield, reflect, ledge-grab and throw bubbles, and a model-hide bit:
  R+D-Up cycles (`dbanim.c:157-168`), renderer `ftdrawcommon.c:81-262`.
- Action-state and animation frame readout: Y+D-Down (`dbanim.c:90-200`).
- Pause and single-frame step: Start pauses, Z steps in VS (`gmvs.c:600-620`).
- Camera debug and free camera (`dbcamera.c`), HUD hide (`dbeffect.c`), item spawner
  (`dbitem.c:290-444`), 5x speed (`dbscreenshot.c:16-35`), CPU handicap grid (`dbcpu.c`).
- The DEBUG MENU itself with Versus rule editing, language, publicity, `DbLevel` spinner,
  and the Sound Test.

Two ways to turn it on:

1. Ship a `develop.ini` through Aurora's DVD overlay so `DVDConvertPathToEntrynum("/develop.ini")`
   succeeds, and feed X/Y from the pad into `db_gameLaunchButtonState` (`dbinit.c:56`). Keeps
   the original semantics; awkward on a handheld (hold a button during boot).
2. One `#ifdef MELEE_NATIVE` override after `gmMain_8015FDA4()` at `gmmain.c:142` reading a
   setting (env `MELEE_DEBUG_LEVEL` first, later the settings file or a menu toggle). About
   ten lines. Recommended.

Side effects to handle before offering it as a "training overlay" option, because level 3 is
a *development* build flavour and changes gameplay:

- Stale-move decay is disabled at `src/melee/ft/ft_0881.c:370`.
- C-stick handling differs at `src/melee/ft/fighter.c:1813, 1832`.
- X+D-Up kills your own stock and pause semantics change in VS (`gmvs.c:575-1435`).
- NaN-position asserts become live (`fighter.c:2439, 2510`, `mpcoll.c:93, 814`); on the
  native build an assert is an `OSPanic` and a crash, so a bug that retail silently survives
  becomes a hard exit.
- New saves unlock everything (`gmmain_lib.c:1268`), which touches the memory card.

The clean design is a native "overlay-only" flag: keep `DbLevel` at Master, and gate the
~8 overlay sites (`db_Setup`, `db_RunEveryFrame`, the pause/step hook, the title-screen Y
exit) on `DbLevel >= 3 || MeleeNativeOverlays`. That is roughly 15 guarded sites in
`db/`, `gm/gmscene.c`, `gm/gmvs.c` and `gm/gmtitlemode.c`, plus tests that the retail
gameplay sites stay untouched. Estimate: 1 day for the raw switch, 2-3 days for the
overlay-only variant including matrix runs on both devices. The DevText overlay draws
through ordinary GX quads (`DrawRectangle`, already exercised by the tournament bracket), so
no Aurora work is expected; verify the debug ortho camera on the 480x320 RG once.

Button-combo problem: the overlays are toggled with R, X, Y plus D-pad chords. On the handhelds
the D-pad *is* the control stick, so the chords collide with movement. Remap the debug chords
in the native pad shim (e.g. Select+face buttons) or expose the toggles in the developer menu
instead; half a day.

## 2. Adding options to the in-game menus

**Verdict: feasible for text-based options; new label textures are blocked.**

How the Options (Settings) menu is built:

- The entry list is a hardcoded table, `mn_803EB6B0[]` at `src/melee/mn/mnmain.c:378-413`
  (Settings row: 6 selections, per-selection anim ranges `mn_803EB4F8`, description string ids
  `mn_803EB684`, handler `mn_8022D104`). Dispatch is a plain switch at `mnmain.c:2305-2381`.
- Entry *labels* are pre-rendered texture-animation frames inside `MnMaAll.usd`
  (`HSD_JObjReqAnim(cursor_part[1], start_frame + 2*selection)`, `mnmain.c:1367-1371`,
  `1477-1479`). You cannot type a new label; the archive is read-only from the disc.
- Entry *descriptions* and submenu text are SIS text, and SIS can render arbitrary
  ASCII strings at runtime via `HSD_SisLib_803A6754` + `HSD_SisLib_803A6B98`
  (`src/sysdolphin/baselib/hsd_3A64.c:81-107, 225-308`). The Rumble screen already prints
  free-form name tags this way (`mnvibration.c:770-780`). Space, digits, A-Z, a-z and basic
  punctuation are covered by the encoder (`hsd_3A64.c:140-221`).

Three routes, cheapest first:

| Route | What it looks like | Cost | Limits |
| --- | --- | --- | --- |
| A. Developer menu as the "Port Settings" screen | `DbLevel`-gated text menu, entries are one struct each (`{kind, callback, label, choices, value*, min, max, step}`, `src/melee/if/types.h:202-212`; root table `soundtest.c:2361-2373`). Add a "Port Settings >" submenu. | Trivial per option once §1 is done. Needs a way in (title-screen Y, or a new hook from the Options menu). | Bitmap DevText look, not the retail UI style. |
| B. Un-hide the existing 6th Options slot | `SEL_SETTINGS_3` is hidden by three lines in `mn_80229938` (`mnmain.c:723-725`); `selection_count` is already 6 and an anim range plus a label frame at 86 exist. Add `case SEL_SETTINGS_3:` in `mn_8022D104` and open a new submenu. | ~5 lines to expose, then a submenu (copy `mndeflicker.c`, 194 lines, or the `mnvibration.c` toggle-row pattern): 2-4 days for a polished N-row toggle screen with SIS text labels and an existing MnMaAll panel model. | The label texture at frame 86 is whatever Nintendo left there; must be checked visually. If unusable, the slot can still be opened but its label will be wrong or blank. Hard cap of 10 entries per list (`mnmain.c:92-94`, `1288-1290`). |
| C. New label textures or a 7th entry | Requires editing `MnMaAll.usd`. | Blocked: the archive layer is read-only (`native/asset_archive.hpp:13-32`), there is no HSD packer, and shipping modified Nintendo assets is not allowed in PortMaster. | A runtime patcher that rewrites the matanim in memory after load is conceivable but is a project of its own. |

Persistence for a new option:

- Size-neutral slots exist in `GamePrefs` (`src/melee/gm/types.h:144-152`): bytes +0x01..+0x07,
  +0x17, +0x1C..+0x1F. The card checksum is recomputed on every write (`baselib/crypt.c`,
  `card.c:1262, 1291, 4690`), and `sizeof(GmSaveData)` stays 0x1790 so `.gci` compatibility
  is kept. Old saves hold zero there, so define 0 as the default. Extend
  `gmMainLib_DefaultGamePrefs` (`gmmain_lib.c:53-55`).
- Native-only settings (deadzone, notches, scaling) belong in a host file, not the card.
  `console-settings.txt` is already read and written by `native/os_runtime.c:159-184`; grow it
  or add a small `melee-native.cfg` key=value parser (half a day) and let `Melee.sh` keep
  setting env vars as overrides.

## 3. Controls

| Feature | Verdict | Where it lands | Estimate and notes |
| --- | --- | --- | --- |
| UCF dashback and shield drop | Feasible | `src/melee/ft/kinds/ftCommon/` turn/dash and guard code (the C equivalents of the UCF Gecko patches: a one-frame stick-velocity buffer for dashback, an extended shield-drop stick zone). | 2-4 days including a frame-by-frame comparison with the UCF 0.84 spec. Must be a toggle: it changes gameplay and should default to on only in VS/Training, never affect replays silently. |
| Notch emulation, radial deadzone, calibration | Feasible | Stick shaping in the pad shim `native/pad_bridge.cpp:68-89`, before the game's octagon clamp (`PADClamp`, Aurora `pad.cpp:1122-1160`). Aurora's deadzone today is a hard cutoff with no rescaling (`pad.cpp:854-895`). | 2-3 days: radial deadzone with rescale, per-axis calibration (min/centre/max capture), snap-to-notch within an angular tolerance, all in one function with a table. Needs the settings layer (§2) and a UI; the developer menu is the fastest UI. |
| Digital "box"-style presets | Partially feasible | Same shim: a modifier-and-direction table producing legal coordinates (the community's B0XX/Frame1 coordinate lists). | 2-3 days for the mapper. The catch is hardware: the RG351P has D-pad, 4 face buttons, L1/L2/R1/R2 and two sticks. A full rectangle layout needs ~18 digital inputs. A reduced preset (D-pad = directions, one modifier on a shoulder, C-stick digital on face buttons) is possible; a faithful box layout is not without an external controller. |
| USB-OTG GameCube adapter | Feasible, CFW-dependent | SDL3 HIDAPI driver for the WUP-028 adapter. Aurora already special-cases GC adapters (`lib/input.cpp:346-350`). | 1 day of code (accept adapter ports, per-port mapping) plus per-CFW testing. Risk: hidraw permissions. PortMaster ports cannot install udev rules, so on some CFWs the adapter works only as root (ROCKNIX runs games as root; dArkOS/AmberELEC vary). Devices without OTG host mode (Flip depends on firmware) are excluded. |
| Bluetooth controllers, per-controller profiles | Mostly exists | Pairing is the CFW's job; SDL sees the pad. Aurora persists `<name>_<VID>_<PID>.controller` profiles with deadzones and maps, and `controller_ports.dat` port assignment. | 1-2 days to expose a profile editor in the developer menu; storage and hotplug already work. |
| Rumble | Exists, unverified on device | `rumble.c` → `PADControlMotor` → Aurora `pad.cpp:975-1017` → `SDL_RumbleGamepad` or the SDL haptic device. In-game Rumble menu already toggles per port. | 1 day to verify on the Flip (has a motor) and add a global intensity setting. RG351P under dArkOS exposes no force-feedback device; expect none there. |

## 4. Training and lab tools

| Feature | Verdict | Notes |
| --- | --- | --- |
| Hitbox/hurtbox viewer, action-state display, frame advance | Exists in the game | Delivered by §1. The only work is the switch, the chord remap and the overlay-only gating. |
| Frame data overlay (startup, active, IASA, on-hit advantage) | Feasible | The data is in the fighter struct (`x914[]` hitboxes, `cur_anim_frame`, IASA flags) and DevText can print it; 3-5 days for a useful readout. Melee's own `dbanim.c` overlay is the template. |
| Save states (training only) | Feasible but the largest single item here | Snapshot = MEM1 (contiguous, `mem1Size`) + the binary's `.data/.bss` (game globals such as `DbLevel`, the gm state machine, fighter static tables) + host state: audio runtime voices, in-flight DVD reads, card bridge, VI/pacing counters, Aurora GX register state and the texture cache (hash-keyed, so it self-invalidates). Restrict to Training mode with no DVD reads pending and a blocking `GXDrawDone`, and it is a bounded problem. Estimate 2-3 weeks including a stress run of load/save across stage transitions. Memory: MEM1 (24 MB default) per slot is a lot on the RG351P; keep one slot in RAM and compress with LZ4/zstd for more. |
| 20XX-style options: infinite shield, action-state display, fixed CPU behaviours | Feasible piecemeal | Infinite shield is one guard in the shield-damage path; action-state display is §1; simple CPU scripts (always shield, tech in place, DI in) are edits in the CPU controller code (`ft/` AI input). 1-2 days per option; expose in the developer "Port Settings" submenu. Full CPU behaviour scripting is a multi-week feature. |
| `.slp` recording | Feasible | The Slippi replay format is public (game-start, pre-frame, post-frame, item, game-end events). All fields come straight from the fighter and item structs at native speed; a writer is 1-2 weeks including a validation pass against a Dolphin recording of the same inputs. Writes ~1-3 MB per match; fine on SD. |
| `.slp` playback with frame stepping | Partially feasible | Playback means re-simulating from the recorded inputs, so it needs determinism (§ below). Replays recorded on this same build replay bit-exactly if the RNG seed is captured (`HSD_RandSeedPtr`, already used by `MELEE_TEST_SEED`) and nothing reads wall-clock. Replays recorded in Dolphin/Slippi will most likely desync: PowerPC paired-single math and estimate instructions (`frsqrte`, `fres`) are not reproduced bit-for-bit on AArch64 even with `-ffp-contract=off`. Ship "replay your own recordings" first; frame stepping falls out of the §1 pause/step hook. |
| Determinism (needed by replay playback and any netplay) | Partial | Good: all game code compiled with `-ffp-contract=off` (`native/CMakeLists.txt:47`), no `-ffast-math`, seeded RNG. Unproven: no test asserts that two runs of the same input script produce identical state. Add a hashed-state-per-frame test to the matrix (1-2 days) before investing in replays or netplay. |

## 5. Handheld-specific

| Feature | Verdict | Notes |
| --- | --- | --- |
| Battery/performance modes with thermal-aware clocks | Feasible in the launcher, not in the binary | Governor code exists in the Flip launcher (`native/platform/flip/launch.sh:41-81`) but the PortMaster `Melee.sh` intentionally has none (PortMaster reviewers expect ports to leave clocks to the CFW; `native/PORTMASTER.md:130-141` is stale and claims otherwise, fix the doc). A "performance" env toggle in `Melee.sh` guarded by `$ESUDO` availability is 1 day. Real thermal-aware scaling belongs to the CFW; the binary can only reduce work: a 30 FPS cap or half-resolution sprites (`MELEE_FLIP_HALFRES_SPRITES` exists) exposed as a "battery" preset. |
| Suspend/resume | Cheap to make robust | CFWs suspend the whole system; the game resumes with a large wall-clock gap. `vi_runtime.cpp:122-124` already resets the retrace phase when late, so no catch-up storm. Remaining work: drain the SDL audio stream on resume and handle `AURORA_PAUSED`/`UNPAUSED` (Aurora raises them, the loop ignores them at `vi_runtime.cpp:129-131`). 1 day plus a test on each CFW's sleep key. |
| Integer scaling | Feasible | Letterboxing today exists only in the Flip present worker (`present_worker.cpp:111-121`); PortMaster builds use Aurora's aspect fit. Adding an integer mode (480x320 gets 1x of 320x240 content with borders; 640x480 is already 1:1) is 1 day. On 480x320 integer scaling shrinks the image to 320x240, so it is a niche option. |
| Optional widescreen | Feasible with known artifacts | The community widescreen codes change the camera projection aspect and the HUD placement. In C that is the perspective setup in the camera code plus HUD/pause-screen offsets; 3-5 days. Expect culling pop-in at the edges (stages cull for 4:3) and stretched 2D scenes unless each is patched. No handheld in the current target list is wider than 4:3 except the Flip's 640x480 (4:3), so low value until 16:9 devices are tested. |
| Low-latency frame pacing and input-latency readout | Feasible | The loop already sleeps until the retrace, polls input, then renders. Poll-late (sleep as close to the frame deadline as possible) is a 1-day change to `VIWaitForRetrace`. A latency readout = timestamp the pad read and the buffer flip, print via DevText; 1 day. True photon latency needs external measurement. |
| Wi-Fi Direct/LAN play between two handhelds | Feasible as delay-based lockstep, not rollback | There is no networking code at all. A minimal design: exchange the 12-byte `PADStatus` per frame over UDP with a fixed input delay (3-5 frames on Wi-Fi), same binary on both sides, RNG seed and rules exchanged at match start. Requires the determinism test above. 3-4 weeks for a usable, menu-driven LAN mode with host/join over the developer menu. Wi-Fi Direct itself is a CFW/network-stack question; assume both devices on one AP or one device hosting a hotspot. |
| Rollback netplay / Slippi online | Not recommended now | Needs full determinism, savestate-per-frame speed (see §4 save states), and Slippi protocol parity. Months, and Slippi parity is impossible without bit-exact PowerPC float behaviour. |

## 6. Data and quality of life

| Feature | Verdict | Notes |
| --- | --- | --- |
| Import Dolphin/GC `.gci` saves | Works today | Aurora's GCI-folder card is standard: drop the file into `ports/melee/runtime/config/melee-native/USA/Card A/`. Aurora can also find an existing Dolphin card folder (`lib/card/DolphinCardPath.cpp:105-145`). A "found saves in ports/melee/saves, import?" prompt in `Melee.sh` is half a day; documenting the path is free. |
| Slippi user accounts | Not applicable | Only meaningful with Slippi netplay. |
| Mod manager separating cosmetic and gameplay mods | Feasible, UI-limited | Aurora's DVD overlay (`aurora_dvd_overlay_files`) replaces or adds FST files at runtime, so a `ports/melee/mods/<name>/files/...` tree is a natural mod format with no engine work. Gameplay vs cosmetic is a manifest flag; "auto-disabled in netplay" only matters once LAN play exists. Wiring: 2-3 days; a chooser UI in the developer menu: 2 more. |
| HD textures | Feasible on the Flip, doubtful on the RG351P | Aurora's texture replacement (`lib/gfx/texture_replacement.cpp`, DDS/PNG, hash-keyed) needs wiring only, but it has never run on the GLES direct-submission path and it costs RAM/VRAM the RG351P does not have (60-140 MB free). 1 week including GLES validation; gate on device memory. |
| Music replacement | Feasible without code | Music is HPS streams read from the disc by `lbaudio_ax.c`; the DVD overlay can substitute `.hps` files, so a mod pack of user-encoded HPS files works once mods (above) exist. A converter to HPS is a separate tool; do not ship one that touches copyrighted music. |
| Colorblind-friendly team colours | Feasible, moderate | Team colours are spread across costume selection, shield colours, name tags and stock icons; 3-5 days to find every site and add a palette switch. |
| UI scaling for small screens | Mostly not feasible | Menus are 3D jobj scenes rendered at 4:3; the readable-size problem on 480x320 is inherent. DevText overlays can be scaled (its setup takes cell sizes, `if_2FF2.c:239-245`). |
| Crash reporting | Feasible, recommend local-only | Today: `LOG_FATAL`/`OSPanic` print a backtrace to `log.txt`, but a raw SIGSEGV prints nothing; the dev-only `flip-crash-trace.so` handler (`native/tools/crash_trace.c`) is not in the package. Compile that handler into the binary (SA_RESETHAND, async-signal-safe writes of pc/lr and the module offsets) and keep the symbol file per release: 1 day, and it removes the LD_PRELOAD dance from every bug report. Symbolicated minidumps via Breakpad/Crashpad on AArch64 glibc are 1 week more. Automatic upload needs a server and a privacy story; keep it opt-in and local until then. |

## 6b. Online play, measured against MeleePad's ONLINE-PLAY.md (2026-09-19)

[MeleePad](https://github.com/chrissotraidis/meleepad) is a static recompilation of the PowerPC
executable running on a Dolphin-derived runtime ("ModernGekko"), so it inherits Dolphin's netplay
whole: *Private Room* and *Direct IP* are Dolphin's fixed-delay input-exchange netplay over ENet
(UDP 2626), peers found through Dolphin's public traversal server with eight-character room codes,
no relay, plaintext, "not Slippi rollback netcode"; its separate Slippi mode is the Slippi
Dolphin stack itself. Their document's own list of requirements, "matching build on both devices,
identical game revision and modules, compatible gameplay settings", is the lockstep contract.

What that means for a C decompilation compiled natively:

| Piece | State here | Effort |
| --- | --- | --- |
| Determinism between two devices | Same binary + same inputs + same seed should give identical state (game code built `-ffp-contract=off`, seeded RNG, no wall-clock reads in game code), but no test proves it. | 1-2 days: hash fighter/item/stage state per frame under `MELEE_MATRIX_TEST`, run the same input script on two devices (or twice on one), diff. Prerequisite for everything below. |
| Input injection point | `native/pad_bridge.cpp` `MeleeNativePADRead()` is the single place the game reads all four `PADStatus`; a netplay layer delays local pads by N frames and fills the remote port from the network there. `online_enabled` / `online_input_delay` settings rows exist already (README's "Online Play: Coming soon"). | Part of the transport work. |
| Fixed-delay lockstep transport (Direct IP / LAN) | Nothing exists. Design: 12-byte `PADStatus` per player per frame over UDP with a fixed delay (3-5 frames on handheld Wi-Fi), frame numbers, redundancy of the last few frames in every packet, stall when a remote frame is late; ENet (MIT, small) or a hand-rolled UDP layer. Start in lockstep from the main menu like Dolphin does: seed (`HSD_RandSeedPtr`), save-file bytes and settings are copied from host to guest at connect so menus, CSS and SSS stay in sync as inputs. | 3-4 weeks to a menu-driven host/join Direct IP mode with a connection screen behind the existing "Online Play" row, plus the on-device test on two units. |
| Room codes / NAT traversal | Dolphin's traversal protocol is small (UDP to `stun.dolphin-emu.org`, hello / connect-please / connect-ready messages); a client is ~500 lines and MeleePad relies on the same public server. No relay when traversal fails, as in MeleePad. | 1 week after the transport, assuming we may use Dolphin's public server; a self-hosted copy is the same code. |
| Rollback (Slippi-style) | Needs per-frame savestates of the whole game state at native speed and full determinism; the `native/FEATURE_FEASIBILITY.md` §4 save-state item is itself weeks. | Months; not now. |
| Slippi online (accounts, matchmaking, Unranked) | Requires bit-exact parity with Slippi Dolphin: PowerPC paired-single and estimate-instruction results are not reproduced on AArch64, so an AArch64 native player desyncs against Dolphin players within seconds; also the Slippi launcher/auth protocol. | Not feasible for this port; MeleePad gets it because it emulates the PowerPC executable. |
| Cross-play with MeleePad's Private Room | Same determinism problem (their game is the PowerPC binary): would desync. | Not feasible. |

Recommendation: native-to-native only. Step 1 the determinism test, step 2 Direct IP / LAN
lockstep between two handhelds on one network (this is the MeleePad *Direct IP* mode), step 3 room
codes through the traversal server (their *Private Room*). Budget 5-6 weeks to step 3 with two
devices on the desk, and expect the input delay to be 4-5 frames on Wi-Fi handhelds.

## 7. Suggested order

1. Debug switch plus chord remap (§1), and the developer-menu "Port Settings" submenu (§2 A).
   Everything else gets its UI from this. About a week including matrix runs.
2. Settings file and stick shaping: radial deadzone, calibration, notches, UCF toggle (§3).
3. Built-in crash handler, suspend/resume drain, integer scaling, latency readout (§5, §6).
4. Determinism test in the matrix, then `.slp` recording (§4).
5. Mods via DVD overlay, then HD textures on the Flip only (§6).
6. Delay-based LAN play (§5) once the determinism test is green.
7. Save states (§4) if training players ask for them; rollback netplay not before that.

Open questions for the user: whether gameplay-affecting features (UCF, notches, widescreen)
should default off to keep the port "retail-accurate", and whether the retail-style Options
slot (§2 B) is worth the visual-check risk versus staying entirely in the developer menu.
