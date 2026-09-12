# Miyoo Flip V2 port: consolidated handoff

Snapshot: 2026-09-11 (second iteration, v79–v143). This is the entry point for
the ARM port, the renderer work, profiling results, and remaining work. Build and
installation details are in [FLIP.md](FLIP.md). The detailed record of this
iteration is [RENDERER_ITERATION.md](validation/2026-09-10-flip/RENDERER_ITERATION.md);
the first iteration's reports remain under `validation/2026-09-09-flip/`.

**State:** the game renders and plays with working audio and controls. Frozen
Onett, moving Onett and moving Battlefield all run at the 60 Hz boundary (median
16.7 ms per presented frame, p95 16.8–20.3 ms, 52–59 presented FPS over
steady 5-second windows; frozen Onett bit-exact with the original reference EFB
capture). Moving Onett's tail stalls were resident-geometry invalidation scans
(v100) and an implicit GPU sync on vertex uploads (v102), not shader compilation.
Moving Onett is render-worker bound at ~15 ms after the shadow passes were fused
and their conversions merged (7 → 4 render passes per frame, v120–v124). Pokémon
Stadium and Hyrule Temple also present at 16.7 ms median; Fountain of Dreams is
GPU-bound (~40 fps, 25k blended point sprites). Four players and menus are
unmeasured.

## Repository and device state

| Item | State at handoff |
| --- | --- |
| Hardware | Rockchip RK3566, Mali-G52, AArch64 Linux, 640×480, ~1 GiB RAM |
| Firmware | Surwish / Buildroot, kernel 5.10.160; app-local Mali g29p1 GLES driver |
| Device executable | v143 (`dist/flip/v143`; v140 plus the reflection pass every other frame and half-res sprites for sprite-heavy frames), launcher with `MELEE_FLIP_ASYNC_FIFO=1` default; launcher shader cache seeded from the trial cache |
| Local source | this tree; Aurora/Dawn working trees under ignored `build/`, patches regenerated and verified against pristine sources |
| Device activity | game stopped between trials, MainUI running |
| ROM | installed at `/mnt/SDCARD/Ports/melee-native/data/disc.img`; no ROM was transferred |
| Symbols | `build/flip-tools/melee_native-v96..v143-symbols` (unstripped, for CPU samples) |

Passwordless ADB at `10.0.0.178:5555` (`build/flip-tools/platform-tools/adb`).

## Where the time went, and what changed

The first iteration left three roughly equal costs: the FIFO translation worker
(28.6 ms busy per frame, joined by the game thread every frame), the render worker
(25 ms), and the GPU (~11 ms main pass). Sampling showed the translator's time
was mostly vertex decoding and texture hashing, and the render worker's time was
mostly inside the Mali driver, not Dawn.

| Change | Mechanism | Effect (frozen Onett) |
| --- | --- | --- |
| Specialized vertex loaders | per-format decode plans, no memset, direct-to-frame | FIFO 28.6 → 24.4 ms |
| Resident display-list geometry | HSD display lists decoded once into GPU arenas; per-call 32-bit indices only; per-frame record stream restores batching | FIFO → 16.4 ms, uploads ~3.8 MB → ~0.3 MB/frame |
| Stable texture identities | identity from image description (incl. wrap/filter/LOD), sampled content verification | FIFO → 13.6 ms |
| Persistently mapped uniform/index streams | FIFO writes GL storage directly; `glDrawRangeElements` required for mapped indices on Mali | render upload 4.1 → 0.2 ms |
| Direct-path trimming | state filtering, persistent texture param memos, sampler state on textures, records via `glUniform1uiv`, no resample pass | ~2 ms render |
| Dawn framebuffer cache | FBOs reused per attachment set | ~2 ms render |
| Asynchronous frames | frame markers in the GX stream; no per-frame join; draw-done waits scoped to the last token | frame = max(game, FIFO, render) |
| Pipeline-state memo | raw-state hash reuses config/shader-info/ref | FIFO 11.7 → 9.9 ms |
| Resident invalidation index | pointer→entry multimap, range query per released region | moving Onett FIFO 20 → 15 ms, tail frames gone |
| Swapchain texture pool | presenter recycles presented GL textures; framebuffer cache keeps hitting | render worker −1 ms (frozen 15.3 ms, moving Onett 15–15.5 ms) |
| Specialized line/point expansion | loader-decoded records copied into quad corners, assembled in local memory | Fountain of Dreams FIFO 46 → 23 ms |
| Mapped vertex stream | streamed vertices decoded into fenced persistently mapped GL storage; no `glBufferSubData` into in-flight buffers | moving Onett render 27 → 17.7 ms (mean frame 27.5 → 17.5), Battlefield 21 → 17.1 |
| Shadow-pass fusion | second shadow map recorded into the first shadow pass with viewport/scissor shifted right; two resolves per pass; per-channel clear/write rules, speculative with exact split fallback | render worker −0.7 ms, passes 7 → 5 (Onett), Fountain fused too |
| Two-target conversion pass | both shadow copies converted in one pass with two color attachments | passes 5 → 4; Onett moving p95 20 → 17 ms |
| Instanced point sprites | one record per GX point, corner from the vertex index, merges add instances | Fountain FIFO 25 → 21 ms |
| Texture arrays | GX textures as layers of shared array textures grouped by size/format/sampler; layer in the uniform record | Onett 360 → 343 draws, Fountain 474 → 445 |
| Scene on the presented texture | EFB passes render into the swapchain texture acquired at encode time; present copy skipped | Onett 4 → 3 passes, render callback 13.8 → 11.8 ms; moving Onett render worker 13.4–14.9 ms |
| Texture verification interval | sampled content re-hash once per 4 frames per texture object | FIFO −1 to −3 ms (Fountain) |

Per-frame budget now (Onett, async, v140): game thread ~5 ms, FIFO worker
~8.5 ms, render worker ~12.3–14 ms, GPU ~11–12 ms. The render worker is the
limiter; most of its time is inside the Mali driver (about 271 moving / 306
frozen draws and 3 render passes per frame: fused shadows, main scene on the
presented texture, dual conversion). Fountain of Dreams (v143): mean frame ~24 ms,
render worker ~22 ms (356 draws, reflection pass every other frame, half-res
sprites), FIFO ~18–20 ms, GPU 12–14 ms; the FIFO worker and the driver's
per-draw cost are now its limiters.

## Current defaults and switches

The launcher (`platform/flip/launch.sh`) selects g29 with direct GLES submission,
threaded presentation, and now `MELEE_FLIP_ASYNC_FIFO=1`. Everything added in this
iteration is on by default and has an opt-out for A/B trials:

| Switch | Default | Purpose |
| --- | --- | --- |
| `MELEE_FLIP_LOADERS` | 1 | specialized vertex decoders |
| `MELEE_FLIP_RESIDENT_DL`, `MELEE_FLIP_RESIDENT_MB` | 1, 64 | resident display-list geometry and its budget |
| `MELEE_FLIP_RESIDENT_RECORDS` | 1 | per-vertex uniform records for resident draws (cross-record batching) |
| `MELEE_FLIP_STABLE_TEXID`, `MELEE_FLIP_TEXTURE_VERIFY` | 1, `sampled` | derived texture identities; `full`/`none` verification |
| `MELEE_FLIP_MAPPED_STREAM`, `MELEE_FLIP_MAPPED_USE`, `MELEE_FLIP_MAPPED_VERTICES` | 1, `all`, 1 | mapped uniform/index/vertex streams; `uniforms`/`indices`/`vertices` bind only one |
| `MELEE_FLIP_RANGE_ELEMENTS` | 1 | `glDrawRangeElements` (needed with mapped indices) |
| `MELEE_FLIP_STATE_CACHE`, `MELEE_FLIP_TEXTURE_SAMPLER_STATE`, `MELEE_FLIP_DIRECT_RECORD_MIN`, `MELEE_FLIP_DIRECT_PRESENT` | 1 | direct-path trimming |
| `MELEE_FLIP_FBO_CACHE` | 1 (Dawn) | framebuffer object cache |
| `MELEE_FLIP_ASYNC_FIFO` | launcher 1 | asynchronous frames |
| `MELEE_FLIP_PIPELINE_MEMO` | 1 | pipeline-state memo |
| `MELEE_FLIP_SWAPCHAIN_POOL`, `MELEE_FLIP_RESIDENT_COPY` | 1 | pooled swapchain textures; GPU-side copies for resident arena uploads |
| `MELEE_FLIP_SKIP_EMPTY_IMGUI` | 1 | no ImGui render pass when the overlay is empty |
| `MELEE_FLIP_FUSE_PASSES` | 1 | record the second shadow map into the first shadow pass (shifted right), two resolves per pass; `MELEE_FLIP_FUSE_TRACE=1` logs splits |
| `MELEE_FLIP_DUAL_CONV` | 1 | convert both shadow copies of a fused pass in one two-target pass |
| `MELEE_FLIP_INSTANCED_POINTS` | 1 | one record per GX point, quad corner from the vertex index (Fountain sprites) |
| `MELEE_FLIP_TEXTURE_ARRAYS` | 1 | GX textures as layers of shared array textures; layer in the uniform record |
| `MELEE_FLIP_TEXTURE_ATLAS` | 1 | clamp-wrapped single-mip textures share 1024² atlas layers (±1 on ~100 pixels of the frozen captures; 0 restores bit exactness) |
| `MELEE_FLIP_TEXGROUP_TRACE` | off | diagnostic: describe each distinct texture bind group once (pair with `MELEE_FLIP_DRAW_TRACE`) |
| `MELEE_FLIP_SCALED_COPY_INTERVAL` | 2 | small color EFB copies (Fountain's 80×60 reflection) render every Nth frame (1 = every frame) |
| `MELEE_FLIP_SCENE_ON_SURFACE` | 1 | EFB passes render into the presented texture; no present copy pass |
| `MELEE_FLIP_FS_VARYING_CONSTANTS` | off | experiment: TEV constants as flat varyings; measured no GPU gain (see report) |
| `MELEE_FLIP_TEXTURE_VERIFY_INTERVAL` | 4 | frames between content re-hashes of a texture object (1 = every bind) |
| `MELEE_FLIP_HALFRES_SPRITES` | 4000 | point count above which runs of point sprites render at half resolution and are composited (Fountain only in practice; −8 ms GPU, +3 passes; not bit-exact when active; 0 disables) |
| `MELEE_FLIP_LAYOUT_VAO` | off | one VAO per attribute layout; neutral on this driver (see report) |
| `MELEE_FLIP_CONSTANT_RECORDS` | off | per-draw uniform record pipelines; −1 ms GPU, +5 ms render worker on this driver (see report) |
| `MELEE_FLIP_PRESENT_BLIT` | off | blit instead of the present copy pass; no gain, ±1 scanout pixels |
| `MELEE_FLIP_SORT_OPAQUE` | off | sort opaque depth-ordered runs by state in the direct path (−0.5–0.7 ms, exact on frozen Onett, order risk elsewhere) |
| `MELEE_FLIP_TEXTURE_PAIRS` | off | texture-bank batching; measured slower (see report) |
| `MELEE_FLIP_DAWN_TIMING`, `MELEE_FLIP_DRAW_TRACE`, `MELEE_FLIP_GPU_TEST`, `MELEE_FLIP_SKIP_POINTS`, `MELEE_FLIP_MAPPED_CHECK`, `MELEE_FLIP_MAPPED_VERIFY_GPU`, `MELEE_FLIP_MAPPED_MIRROR`, `MELEE_FLIP_MAPPED_BIND_DAWN`, `MELEE_FLIP_FUSE_DAWN`, `MELEE_FLIP_FUSE_RESET`, `MELEE_FLIP_DUMP_EFB_PASS` | off | diagnostics only (the last three: Dawn renders fused passes / direct-path memo reset at the fused segment / EFB read-back at pass start) |

Earlier experiments (`MELEE_FLIP_STREAM_UPLOAD`, `MELEE_FLIP_NATIVE_SPECIALIZED`,
`MELEE_FLIP_VERTEX_CACHE`, `MELEE_FLIP_MAP_UPLOAD`, `MELEE_FLIP_DECODE_DIRECT`,
`MELEE_FLIP_RELEASE_TEXOBJ`) remain opt-in and were not re-evaluated.
`MELEE_FLIP_PREWARM_PIPELINES=1` currently crashes at startup and must stay off.

## Validation

- Frozen Onett EFB capture `da46d4a79a8f…` matches the v77/v79 reference for every
  default-configuration build v80–v136 (async included); frozen Fountain of Dreams
  `cc2fea00f05f…` matches through v136. From v140 the default configuration
  (texture atlas) differs by ±1 on 109 / 172 pixels (`8a3713fa00a5…` /
  `87712e4c1eab…`); `MELEE_FLIP_TEXTURE_ATLAS=0` reproduces the references (one v117 run differed in the player
  indicators and never reproduced; see the report). The scanout capture
  changed at v99b (`eaf1f2147d79…`, 20 pixels differ by ±1); the EFB is the
  criterion.
- Moving Battlefield and moving Onett 60-second soaks complete without GL errors
  or asserts; memory is stable after the FIFO compaction fix (v97).
- `melee_flip_vertex_test` passes on the device with new mixed-format cases;
  `test_flip_prepare.py`, `test_flip_launcher.py`, `test_flip_deploy.py` pass.
- Both dependency patches apply to their pristine sources
  (`git apply --check` against the extracted Dawn archive and a stashed Aurora
  checkout).
- Saving: `native_hsd_card_test` (host, ASan/UBSan) runs Melee's `lbcardnew.c`
  save path against an in-memory card: create (11 blocks, 90112 bytes, banner
  and icons placed as on console), reload, read all sub-files, rewrite, and a
  corrupted sector is rejected (error 3). On the device, boot with
  `MELEE_INPUT_SCRIPT=/tmp/melee-create-save.input` (`native/tests/create-save.input`,
  answers Yes) and then `native/tests/load-save.input`; the save appears at
  `data/config/melee-native/USA/Card A/01-GALE-SuperSmashBros0110290334.gci`
  and `native/tools/check_gci.py` validates the pulled file.
  `native/tools/flip_save_trial.sh` is the device-side runner.
  Device result (v149, 2026-09-12): creating a save on an empty card and
  booting again with the save present both reach the main menu with no crash;
  both `.gci` files validate (11 blocks). The title-screen crash of v146 was a
  32-bit `int` temporary in `lbcardgame.c` truncating a banner pointer. Logs and
  saves are in `native/validation/2026-09-11-flip-saves/`. Remote deployment
  used `flip_holder.sh` and `flip_http_deploy.sh` (see FLIP.md, Remote access).

## Measurement

`MELEE_FLIP_PROFILE=1` (set by the trial tool) enables, besides the existing
records, `[perf-breakdown]` on the game thread (game/FIFO/render/pipeline-wait
split per presented frame), `[flip-render-phase]`/`[flip-render-callback]` on the
render worker, and `[flip-resident]`. `MELEE_FLIP_DAWN_TIMING=1` adds
`[flip-dawn-pass]` per-pass and `[flip-dawn-submit]` timing from the Dawn patch.
`[flip-gl-calls]` counts the direct path's GL calls per frame and
`[flip-batch-resident]` the first merge-failure reason per resident draw;
`[flip-upload-phase]` splits the render-worker upload phase.
`native/tools/flip_gl_bench.c` measures raw driver call costs. CPU samples
(`--cpu-samples`) must be analyzed against the matching
`build/flip-tools/melee_native-vNN-symbols` binary.

## Next work, in priority order

1. **Render worker below ~12 ms** (moving Onett sits at 12.3–14 ms: main pass
   ~8–9 ms of driver time for ~271 draws; fused shadows and dual conversion ~1 ms
   each). Measured and rejected: per-draw uniform records (+5 ms), one VAO per
   layout (neutral), TEV constants as flat varyings (no GPU gain), the existing
   uber shader (250 ms frames, not exact), texture pairs (no merges, 5,000 binds).
   Done: swapchain pool, shadow-pass fusion, two-target conversion, texture
   arrays, clamp-texture atlas, scene rendered into the presented texture
   (7 → 3 passes, 360 → ~280 draws). What remains: pipeline breaks (~220
   adjacent pipeline changes per Fountain frame, mostly TEV/channel/vertex-layout
   differences; a purpose-built uber path or a "superset vertex layout" would be
   needed), repeat/mirror textures (21 % of textures; an atlas for them needs
   shader-side wrapping with gutters), opaque sorting (opt-in, −0.5 ms), fewer
   HUD draws. **Fountain of Dreams** is render-worker bound at ~22 ms with 369
   draws and 8 passes (the reflection pass alone is ~150 draws, 3 ms); its GPU
   lever (half-res sprites, −8 ms GPU) is implemented and opt-in until the CPU
   side drops below the GPU time. Its FIFO worker (~18 ms) splits into indexed
   s16 decoding, memcpy of display-list indices, uniform builds and texture
   hashing.
2. **Compile stalls on a cold shader cache** (`pipeline_wait_ms` is ~0 when warm):
   non-blocking pipeline creation (skip the draw, Dolphin style), repaired
   prewarming, or a shipped cache from scripted matches.
3. **Fountain of Dreams** (~37 fps): ~25k blended point sprites cost ~8.4 ms of the
   17 ms main-pass GPU time and half the FIFO time; without them the stage is still
   at ~48 fps from ~490 draws per frame (the reflection is a second camera render
   of the fighters copied to an 80×60 texture: ~150 draws, little GPU). Fewer
   fragments per sprite (half-resolution particle pass) and cheaper draws are what
   remain; per-draw constant records were measured and rejected.
4. **GPU headroom.** Per-draw uniform records do not pay on this driver (any
   per-draw uniform change costs ~15 µs of CPU). Remaining GPU levers are
   partial clears at EFB copies and the shadow-pass fusion in item 1. Dynamic uniform-record indexing in TEV fragment shaders costs
   ~2.7 ms of the ~13 ms GPU frame; consider per-draw constant records when the
   CPU side allows it.
5. Validate more stages, four-player matches, menus and longer sessions before
   claiming solid 60 FPS.

Design references remain Dolphin's ARM64 vertex loaders, vertex loader manager,
fenced GL streaming, and hybrid ubershaders; the resident display-list cache is
the native-port analogue of uploading meshes once, which an emulator cannot do.
