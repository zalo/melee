# Native port: recurring problems and preventive fixes

Status: 2026-09-08. Scripted Onett matches work; the port is incomplete. The user has authorized app
launches again. Automated native runs now navigate through the main/VS menus,
choose a human fighter and CPU, load Onett and execute movement, combat, grabs,
damage and pause/resume. One-minute runs have reached visible Results and returned
to character selection. Other end states and saving remain under active repair.

## Findings from scripted combat and visual checks

- A complete `match-controls.input` sequence passed without sanitizer errors on
  2026-09-08. Longer runs exercised Zelda/Shiek transformations and reached the
  end of the timed match. These are progress checks, not proof of a finished port.
- The common `UnkFlagStruct` must retain numeric bit positions. Setting byte 1
  enables normal fighter drawing (`b7`) on GameCube; unreversed native fields
  selected debug geometry (`b0`) instead and made the fighters invisible.
- Fighter material templates now reference their actual globals; the original
  synthetic class-info overlay assumed they followed `ftMObj` in memory.
- Stock-icon selection had an uninitialized base for ordinary characters, and
  its wrapper omitted its return. Native code supplies both explicitly.
- HUD digit material animation access used `HSD_AObjDesc` as an overlay for
  `HSD_MatAnim`; their pointer offsets diverge on LP64. Native access now follows
  the real material and texture animation types in every digit-update path.
- Color-animation command stacks span six original words. The shared three-slot
  placeholder and mixed-width color overlay corrupted nested loops. Native
  stacks use six pointer-width slots; a nested-loop test checks the following
  color-animation ID remains intact.
- Capture-wait and Super Scope code contained fixed Fighter-offset overlays.
  Native access now uses actual motion variables and the typed grab/victim fields.
- Yoshi visibility data contains two `TempS` records; its second pointer and
  record boundary are now verified against that layout.
- Null `GET_FIGHTER` calls used solely to force original stack padding are omitted
  natively in wall reflection and Yoshi specials.
- Particle deletion now addresses the actual particle-list heads, joint table
  and allocation counter rather than an overlay across unrelated globals.
- Match ranking loops handle six slots, so their temporary score array needs six
  elements. Pokémon bonus counts use `StaleMoveTable.x848[kind - It_PKind_Start]`,
  not an out-of-bounds index from the earlier eight-counter member.
- The runner now captures a match image by test PID and records external debugger
  reaping as a failed run instead of losing the result to an uncaught exception.

## Runtime work after the preventive pass

- Added complete CSS/SSS scene and animation tables. Both language variants
  are included in the asset integration tests.
- Converted PObj shape sets and their pointer tables. CPU-read shape vertices
  receive native numeric byte order; packed index streams retain disc order.
- Added joint spline and particle-list union variants, effect descriptors and
  native command/texture banks. All 22 effect archives pass conversion plus C
  layout checks. Particle bytecode float operands now decode big endian.
- Replaced the Classic mode global overlay with native owned state/references.
- Guarded effect and stage-selection sentinel indices before indexing arrays;
  the hidden stage preview also skips animations when no stage is selected.
- Fixed the game-state bit-set operations whose variable shift can reach bit 31.
- Added retrace-based input scripts for repeatable menu and match-loading tests.
  A completed input script is not evidence of completed gameplay; inspect the
  resulting screen and sanitizer log.
- Original GameCube build remains byte-identical after these changes.

## Match-loading work and additional recurring causes

- Stage core schemas now handle all `Gr*.dat` files, including explicit all-ones
  descriptor sentinels. Stage-specific parameter tables still need individual
  schemas beyond Onett.
- Item common data and article graphs convert with real C layouts. Both ItCo
  language variants traverse 98 articles in the integration test. Fighter item
  lists also contain supplemental joints, visibility tables and grapple models;
  their element types come from the corresponding game consumers.
- Archive external-reference chains bind materialized fields by symbol, including
  rebinding. Serialized dynamics inputs are numeric coefficient arrays; fighter
  per-animation dynamics tables hold bone counts, not FigaTree pointers.
- Fighter animation flags now preserve their numeric bit positions. Animation
  addresses remain pointer-wide through both transfer paths and the shared table
  aliases. All Fox animation bundle archives are materialized by a separate test.
- Archive ownership follows its source buffer, not a temporary stack descriptor.
  A regression test reparses two buffers through the same descriptor and verifies
  that the first animation/script survives until its own buffer is released.
- Finished model-ID pointer-width fixes in metal, Kirby and stage materials.
  Native card state now owns its aligned CardState instead of overlaying packed
  unrelated globals. Save persistence has not passed an end-to-end test yet.
- ARQ callbacks recover their queue node from the embedded request and index native
  state lists by element. Original hardcoded structure offsets truncated or
  misplaced those addresses.
- Collision line indices now use typed subtraction. Eight endpoint traversal
  routines also stop casting collision-array addresses through 32-bit integers.
- Input scripts wait for real scene readiness and feed the normal PAD path. A
  successful script is still not an assertion of correct rendering or gameplay.
- Fighter corpus checks now cover all 34 base files and 6,245 animation chunks
  from 33 bundles. Materialization does not prove all runtime consumers correct.
- Unknown `ftCommonData` words are numeric, not host pointers. Their mistaken
  LP64 expansion shifted later parameters, including entry duration, causing
  division by zero. Native offset assertions cover the numeric tail and colors.
  Applied the same correction to numeric unknown fields in Link, Purin, Samus
  and Yoshi attributes.
- HUD joint traversal now uses JObj children and real material-animation types.
  Player mapping accesses use the actual mapping array, and match kill counters
  cover six players. Extended action-stat counters stay within their owning
  structure instead of indexing beyond a smaller embedded array.
- Particle/generator handles remain pointer-wide across creation and traversal.
  The background-flash state owns its real host allocation and GObj user data.
- The central fighter opcode view needs the same high-bit ordering as command
  operands. It was missed by the generated operand views; a regression checks
  both interpretations of a known command. Halfword casts into host-order script
  words are also wrong: the landing-effect ID now extracts the low 16 bits.
- `GXEnd` was empty in the original SDK, so several reconstructed routines omitted
  it. Native rendering requires explicit primitive boundaries. Added ends to
  particle batches, trails, debug shapes, afterimages and screen overlays in
  eight files; never hide this error by dropping geometry or disabling checks.
- Rotation getters/setters transfer four floats even for Euler rotations. The
  IK helper now reserves quaternion-sized storage instead of casting a Vec3.
- Stage-start callback readers must match the pointer-valued queue writers.
  Fixed the shared callback consumer used by Onett and other stages. Onett's
  unselected-car sentinel is only resolved after its state machine chooses a car.
- The bounded input runner stores each sequence, log and outcome, samples hung
  threads and reports failures. Gameplay inputs support simultaneous buttons,
  both sticks, shields, grabs and pause/resume. Scene transitions can still fail
  if menu input is missed; such a failure is reported, not silently retried.
- The native DevCom cancellation path preserves its callback/argument unless
  replacement flags request otherwise, including during the final ARAM copy.
  The original path cleared the callback in that timing window, leaving the
  synth bank queue waiting forever. A dedicated test covers cancellation before
  DVD completion, between DVD and ARAM completion, and explicit replacement.
- Shared stage animation callbacks now carry an AObj pointer and pointer output.
  Effect animation queues use real JObj pointers in all three dispatchers, and
  effect parameters directly address their separate table. IK square-root
  rounding uses an owned volatile scalar rather than a negative stack index.

## Findings from actual failures


| Class | Observed cause | General correction | Remaining work |
| --- | --- | --- | --- |
| Pointer width | Heap/stream addresses and model IDs were stored in 32-bit scalars | Use pointer-width values throughout each host-address chain, including consumers and archive fields | Packed card commands and many game-specific casts remain |
| Global overlays | Trophy state and character selection treated separate globals as one structure | Use one owned state block or explicit typed references to actual objects | Additional overlays are listed in the inventory |
| Stack layout | Menu camera wrote at `&pos + 0x14`; menu option list exceeded four entries | Pass the actual object; size arrays from the option table | Other hardcoded stack/data offsets require semantic inspection |
| Fixed sizes | Pointer arrays shrank when a fixed byte size was divided by native pointer size | Preserve element counts; use `sizeof` for host storage and individual cleared fields | Original disc sizes must remain fixed, so blanket replacement is unsafe |
| Asset endian/layout | SIS glyphs, movie headers, audio streams and mixed archive records were interpreted as host memory | Decode numbers and materialize typed host records; preserve encoded byte streams where their consumer decodes them | Fighter/stage/scripts, some descriptor variants and shared references |
| Integer semantics | Signed top-bit shifts, negative sign-extension shifts, float-to-byte overflow | Unsigned bit operations, explicit signed decoding, integer intermediate before extracting encoded low byte | Variable shifts and other conversion candidates |
| Null member addresses | TEV code formed `&null->member` while walking lists | Explicitly retain null at list boundaries | Other lists need individual inspection |
| Hardware timing/ABI | Async CARD callbacks, VI updates, PAD and movie APIs had different semantics/layouts | Explicit adapters and callback ordering tested separately | Actual save queues and directional input still need runtime validation |

## Changes in this preventive pass

- Converted top-bit literal masks in 46 source/header files to unsigned shifts.
  The original-only `lbtrigf` expression is preserved conditionally for its
  matching build. These masks are distinct from arbitrary variable shifts.
- Completed the HSD ID chain for host addresses: IDEntry, table APIs, jobj IDs,
  geometry/constraint lookups, animation descriptor IDs and archive conversion.
  A regression test inserts two IDs with identical low 32 bits, updates one,
  removes it and verifies that the other survives.
- Replaced character-selection `CSSAllData` overlay access with typed references
  to the actual icon, door, tag and miscellaneous globals in native builds.
  No initializer contents were changed. The model/animation-table boundary now
  uses `sizeof(CSSSceneModels)` rather than 16 bytes. Its archive root still
  requires a complete schema before character selection can run.
- Removed stock-HUD pointer arithmetic that first formed an address before its
  array and then added the same offset back.
- Corrected reset sizes for the full game state, magnifier state and player-HUD
  array. The HUD reset deliberately preserves the model descriptors following
  its player array; clearing the entire HUD would be wrong.
- Corrected signed animation-stream decoding, four text-scale low-byte
  conversions and the remaining null TEV-list transition.
- Kept native differences conditional where necessary to retain the exact
  original GameCube output. Source changes that look equivalent can still alter
  CodeWarrior code generation; the original checksum is a required regression.

Earlier fixes include the trophy state block, trophy archive-array count,
menu camera/option stack buffers, safe filename copies, stream ratio/end-address
handling, deferred CARD completion and movie ABI conversion. They are captured
here so future work searches for the same causes instead of only the next crash.

## Reproducible whole-source inventory

Run from the repository root:

```sh
python3 native/tools/scan_portability.py --output native/portability-findings.json
```

The read-only scanner covers every `.c` and `.h` under `src`, including original
conditional branches. It records paths, line numbers and matching code for ten
classes. Comments are omitted while line numbers are preserved. It does not
parse C types or prove whether a match is reachable in the native build.
Scalar casts, intentional disc-format offsets and original-only code can be
valid. Counts overlap and must not be presented as a count of confirmed bugs.
Generated SDK headers and third-party dependencies are outside this inventory.

This is whole-source pattern coverage, not a claim that every line or every
candidate has been manually validated. Inspect declarations, callers, allocation
size, field meaning and lifetime before changing an address or offset.

## Priority unresolved groups

1. `hsd_3A94.c`, `hsd_3B27.c`: packed 32-bit queues, pointer-valued parameters
   and disk save layout. The embedded CardState is now typed, but the queue
   still needs a coherent host layout and real save/reload verification.
2. Character selection now works in scripted runs and its archive schema is
   implemented. Wider fighter and menu coverage remains to be exercised.
3. `hsd_3A76.c`: SIS jump/call commands and their 32-bit pointer/return-stack
   encoding. Glyph endian fixes do not solve command-pointer ownership.
4. Fighter/stage and other archive schemas: bitfields and relocated references
   need schema-specific conversion; neither raw copy nor global byte swapping
   is sufficient.
5. Remaining fixed allocation/copy lengths, variable shifts and integer casts:
   use the inventory and compiler diagnostics to prioritize actual pointers and
   host objects. Do not enlarge fields that encode GameCube addresses or file
   offsets without changing the format boundary explicitly.
6. Input delivery and sound fidelity: CPU tests alone do not verify actual
   interaction, rendering, streaming sound, effects or timing.

## Verification boundary

Build the full native executable and run `ctest --test-dir build/native -L melee
--output-on-failure`. These tests do not open the game or an audio output device.
Use `ninja -j8` for the original GameCube checksum regression. The optional
`MELEE_SANITIZE_GAME=ON` configuration instruments game/runtime code but not
prebuilt dependencies. A successfully linked executable and passing isolated
tests do not replace boot/menu/match/save acceptance tests.

Historical verification before app launches resumed: full native executable linked; all 12
registered tests passed; original `main.dol` checksum passed with all 19,828
functions matching. No application or audio-device launch was performed.

Compiler follow-up: `ruby native/tools/check_uninitialized.rb` checks all 984
native game/HSD C files and writes `build/native-uninitialized.log`. The first
pass reports candidates in 18 files. Some are impossible switch arms following
assertions or conditions already implied by the enclosing branch; they must not
be fixed by blanket zero-initialization. The no-damage color-animation result was
confirmed to require a defined false result and fixed natively.

Particle material follow-up: `psSetupTev` treated an `HSD_Particle` as a
32-bit word array and read/wrote word 1 as its material flags. On LP64 this
is the upper half of the linked-list pointer, so texture sampling/alpha could
be disabled and the list link could be corrupted. Native code now accesses
`HSD_Particle.kind`. The particle TEV test exercises all 16 flag combinations,
checks texture alpha participation, and verifies the full list link survives.

The results overlay's first word is the numeric `MatchEnd` timer. Treating it
as `UNK_T` shifted every results field by four bytes on macOS. It is now a
native u32 with assertions that player and bonus arrays alias actual MatchEnd
fields. The rules-menu child getter similarly must return a full HSD_JObj
pointer, not an int.

Native expression bytecode stores float payloads as four-byte bits in pointer
slots, not by reading eight bytes from a float. A dedicated sanitizer test
executes float arguments, arithmetic, conversion and trigonometry. Unsigned
high-bit counting and native expression bit masks avoid signed-shift UB.

VI presentation now logs measured frame submissions over five-second wall-clock
windows as `[perf] presented_fps=...`, separately from its 60 Hz target. This is
CPU-side presentation throughput, not a display scanout measurement. Initial
menu measurements were approximately 58-60 FPS in the Debug/sanitizer build.
The input runner records an explicit MELEE_TEST_SEED (default 1); normal launches
retain clock-based initialization. The seed alone is not proof of determinism.

Measured rendering follow-up: optimizing Aurora core/GX with `-O2` in Debug
keeps its assertions and the game's sanitizer instrumentation enabled. After
this change a Fox/Peach Onett run reported approximately 59-60 game-render FPS,
versus approximately 39-51 FPS with the unoptimized renderer in earlier matches.
These are observed runs, not a controlled same-opponent benchmark or a guarantee
for every scene. The VI logger also counts completed game render-loop passes.

The Rules-menu test now reduces Time from two minutes to one. A second fixture
selects one Stock and runs toward the left blast zone to reach results sooner.
One-minute timer and natural match-end bookkeeping were observed live. Results
loading now materializes `pnlsce` and `flmsce` as SceneDesc graphs; the archive
layout test traverses their 26 and 115 joints. Demo motion bundles retain raw
embedded archive bytes and full-width animation base addresses.

A Pokémon spawn animation read its seventh scale after completing six samples.
The caller immediately transitions to the completed state and never consumes
that delta. The native path stops interpolation at the terminal sample.

Video evidence: `build/native-runs/20260908-165809/match.mp4` is a verified
21-second silent recording of Fox/Peach combat, hit effects and a knockout.
The reusable ScreenCaptureKit helper captures only the process window, even
when another app is foreground. The OS screencapture command's video mode did
not honor its window selector, so that implementation was removed and its
incorrect recording deleted. The replacement initializes AppKit before SCK.

Results follow-up: the port now stores the display images, object arrays and
state in one real native ResultsDisplayLayout, instead of depending on separate
globals being contiguous. Camera settings use the actual named descriptor,
character camera offsets, and slot offsets. Packed halfword constants are
unpacked in GameCube order, including signed score positions. The camera factory
returns the created object on the native path.

Run `20260908-170624` completed a one-minute Fox/Fox match, entered scene 5,
and returned to scene 8 without a reported sanitizer error. Its Results screenshot
at two seconds was black, so visual acceptance remains open; capture is now at
five seconds. The one-stock fixture is experimental: CPU interference can keep
the proposed run-off sequence from ending the match, causing a scene timeout.

Run `20260908-170930` completed Fox versus Zelda/Sheik through Results and back
to character selection. Its five-second Results capture visibly contains the
winner model, ranks and statistics. Missing score signs were traced to packed
Shift-JIS integer constants, now represented as explicit bytes on the native path.

Run `20260908-171329` exposed the missing ItemDynamics collision tail. The
serialized record is 16 bytes, not eight: collision count and descriptor pointer
follow the bone dynamics fields. The schema now materializes both, and itcoll
uses the actual native type. Tests validate collision counts and finite geometry
for all 98 articles in each language variant. All 21 native tests pass.

Run `20260908-171953` reached Sudden Death but UBSan caught a sixth team read
from the five-entry team standings array in gm_80166CCC. The native loop now
uses GM_MAX_TEAMS; the original matching path is unchanged. The follow-up run
was `20260908-172543`; it instead exposed a Kirby effect failure before match end.

Kirby's Final Cutter effect (0x494) used the GameCube Fighter offset 0x5E8
and treated the bone array as four-byte pointers. The native path now uses
Fighter.parts[44].joint and parts[1].joint, preserving the original 16-byte
bone-record indexing. Its rotation callback also used an HSD_JObj overlay to
read Fighter.facing_dir at original offset 0x2C; it now reads the typed field.
These fixes compile, but a subsequent run must exercise Kirby's up-special
to establish runtime acceptance.

Run `20260908-172721` completed Fox/Donkey Kong through Results and back to
character selection without a reported sanitizer error. Its Results screenshot
visibly verifies the corrected minus/plus signs (-1/+1 totals). It did not
exercise Kirby's effect or Sudden Death, so those specific paths remain open.
The original GameCube build still reports `main.dol: OK` after both fixes.

Manual input and frame presentation (2026-09-08): `Play Melee.command` launches
without replay input and does not terminate after a test. Existing SDL keyboard
mapping remains active whenever the game window has focus.

The earlier 21-second clip contains five short black intervals using ffmpeg
blackdetect on the inner game area (99% pixels below 5% brightness). VI previously
ended and began an Aurora frame at every retrace, even without a completed game
image. It now waits for HSD's EFB copy completion and keeps the recording frame
open across extra retraces. Explicit VI blanking transitions still present.
Telemetry records held_retraces separately. Run 20260908-173628 completed the
minute match, Results and return to CSS without sanitizer errors. Its runner
reports failure because ScreenCaptureKit failed to process the first sample;
there is no successful post-fix video analysis. The user requested no further
captures and immediate manual control. All 21 native tests and the original
GameCube checksum passed before the equivalent hook placement adjustment to
also cover the completed bottom-half render pass.

## Standalone app packaging (2026-09-08)

`dist/macos/Melee Native.app` and its ZIP are assembled by package_app.rb from
the unsanitized Debug build in build/native-app, with Aurora's Debug -O2 retained.
The bundle contains 20 files: executable, four relocated libraries, notices,
readme, plist and signature. No build-tree assets, image files, console firmware,
or sanitizer runtime are copied. A SHA-256 manifest accompanies the ZIP.
Signatures verify locally; the app is not Developer ID signed or notarized.

The native picker completes AppKit launch before displaying, supports files
without registered UTIs, exits on Cancel, and reports unreadable images. The
disc loader validates GALE01 revision 2. GUI selection of the user's CISO was
verified to boot the final bundle; a cloned revision-1 header was rejected and
the test clone removed. This does not establish redistribution rights for
recovered game code.

The previous DSP-ROM-derived coefficient include is gone. prepare_audio.py
generates three normalized four-tap windowed-sinc banks without any input data.
Audio tests verify constant-signal gain at half, normal and double rate across
all three banks. These filters approximate, rather than reproduce, hardware.

The unsanitized app exposed three vsnprintf calls using size -1 despite finite
local buffers. Native calls now use their actual sizes, and SIS scratch buffers
allow up to seven encoded bytes per source byte plus terminators. Original
GameCube code is unchanged and its checksum still matches. All 21 tests pass.

Packaged run 20260908-185604, launched with cwd /tmp and no capture, traversed
boot, menus, Onett, Sudden Death, Results, and trophy unlock (scene 39). The test
failed waiting for CSS because it has no trophy-unlock dismissal step; it did
not complete the scripted return-to-CSS assertion. GUI launch of the final
bundle separately loaded the selected CISO and rendered the initial save prompt.

## Sequential VS matrix, September 8

The native matrix uses fresh processes, the normal menu/preload route, explicit
stage/character assertions and 15 seconds of CPU combat. Common item cases
spawn the selected kind repeatedly. Numeric GPU readback samples the central
playfield without saving images. A visible center catches HUD-only black output,
but does not prove that every fighter, texture, effect or item action is correct.
Per-case logs and executable hashes are stored in `build/native-matrix`.

Recurring porting findings from this sweep:

- Keep serialized numeric words fixed-width even when the decompilation labels
  them as unknown pointers. Beam Sword's attribute prefix is numeric; widening
  it moved its trail parameters beyond the allocation. Byte/color ranges must
  remain bytes instead of being swapped as whole words.
- A converted pointer-bearing record needs a matching native schema and native
  allocation size. Stage collision tables, Rainbow Cruise entries and Game &
  Watch item draw descriptors exposed separate instances of this mistake.
- Original expressions sometimes walk across adjacent globals or alias an
  unrelated structure solely to reproduce PowerPC addressing. Native code must
  name the actual global or typed field. Brinstar, Venom, Big Blue, Icicle
  Mountain, Kirby Stone and carried-crate animation IDs exposed such cases.
- Native bitfield order must be defined explicitly where data is also accessed
  as packed bytes or halfwords. This affects stage callbacks and Big Blue's car
  state, and cannot be solved by widening pointers alone.
- Animation stream ownership must outlive the archive scratch buffer. Native
  FObj constructors now copy their bytecode and release it with the object.
  The secondary animation loader also uses its secondary buffer consistently.
  `native_fobj_stream` exercises a track after its source buffer is overwritten
  and freed.
- Test setup must let CSS preload the selected character. Mutating only the
  final VS record can create test-induced stalls or mismatched assets. External
  stage IDs must be compared with the external stage query, not internal IDs.

All changes that differ from the original layout are native-only. The original
DOL remains byte-identical after these changes. Matrix smoke passes are not
full-game acceptance: saving, adventure/target stages, all costumes, every move,
all Kirby copy abilities and every item/Pokémon behavior need separate coverage.

The SFX request callback can run on the native DVD worker before the submitting
function returns. The two callback-driven audio loading sites now publish the
DVD entry ID before queuing the load, so completion can identify its slot even
with cached reads. Three consecutive Pichu smoke runs passed after this change.
The earlier Pichu bank overflow was intermittent; this establishes the repaired
publication ordering, not proof that every possible audio scheduling issue is
covered. Optional asset tracing records bank usage and requested sizes.

## Blank world and hookshot follow-up, September 8

The original non-black GPU check incorrectly accepted solid blue backgrounds.
The replacement ignores the first countdown sample and requires color and edge
variation in at least 80% of subsequent samples (minimum three). This detects
blank scenes, but is not a pixel-accuracy comparison or proof that all effects
are correct. Matrix tests also reject non-finite camera matrices.

Camera_ApplyQuake overlaid CameraModeCallbacks and adjacent globals with a
synthetic CameraStaticData struct. Their native alignment/order differs, so the
viewport height read as zero. Even zero shake produced 0 * infinity = NaN,
contaminating both camera eye and interest and hiding all world geometry. The
native path now reads cm_803BCB64 directly. A controlled Onett run changed from
one color after the countdown to hundreds of colors throughout combat.

Young Link's hookshot used it_802A4BFC_sqrtf_offset, whose retail stack-matching
trick wrote six floats past a scalar local. The native path now calls sqrtf;
the original path remains unchanged. A native Young Link combat run completed
without the former return-address corruption. The exact scalar-address offset
pattern was searched across source; this was the only occurrence.

The Brinstar damage callbacks also required native typed access to their acid
and platform state. The old fake union aliases wrote damage floats into nearby
joint pointers. The matrix preflight exercises both real callbacks and verifies
that adjacent pointer fields remain intact.

Pikachu's down-special mixed SpecialHi integer fields with SpecialLw's pointer
and state fields. On LP64, the state write occupied the high half of the Thunder
pointer. All down-special state accesses now use the native SpecialLw layout.
This also applies to Pichu's shared implementation. A combined Corneria / Captain
Falcon versus Pikachu / Capsule case reproduced the failure under ASan and then
passed after the fix.

Combined matrix mode rotates both fighters and common items across remaining
stages. `MELEE_TEST_COMPLETED_STAGES` explicitly lists stages already tested;
with it unset, the combined plan includes all 29 stages. Both fighters are
preselected before preloading, and their actual IDs are checked after loading.
The runner stops on the first failure by default, retaining completed cases.
A combined pass covers each constituent stage, fighter and item without a
separate fixed-opponent character sweep, but not every cross-product combination.

Kirby's Yoshi copy archive uses a hat/parts prefix, an egg joint, four animation
roots and an egg article. It must not fall through to the generic ftData schema.
The native hat's extension storage accommodates the six/seven-entry records
already accessed by Yoshi and Game & Watch code. The Yoshi copy archive is now
part of the existing sanitizer-backed match-asset regression test.
