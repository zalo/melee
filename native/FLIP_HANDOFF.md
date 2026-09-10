# Miyoo Flip V2 port: consolidated handoff

Snapshot: 2026-09-10 (second iteration, v79–v114). This is the entry point for
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
Moving Onett is render-worker bound at ~15–15.5 ms. Pokémon Stadium and Hyrule
Temple also present at 16.7 ms median; Fountain of Dreams is GPU-bound (~37 fps,
25k blended point sprites). Four players and menus are unmeasured.

## Repository and device state

| Item | State at handoff |
| --- | --- |
| Hardware | Rockchip RK3566, Mali-G52, AArch64 Linux, 640×480, ~1 GiB RAM |
| Firmware | Surwish / Buildroot, kernel 5.10.160; app-local Mali g29p1 GLES driver |
| Device executable | v114 (`dist/flip/v114`; v108 defaults, empty ImGui pass skipped, opt-in experiments), launcher with `MELEE_FLIP_ASYNC_FIFO=1` default |
| Local source | this tree; Aurora/Dawn working trees under ignored `build/`, patches regenerated and verified against pristine sources |
| Device activity | game stopped between trials, MainUI running |
| ROM | installed at `/mnt/SDCARD/Ports/melee-native/data/disc.img`; no ROM was transferred |
| Symbols | `build/flip-tools/melee_native-v96..v114-symbols` (unstripped, for CPU samples) |

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

Per-frame budget now (frozen Onett, async, v106): game thread ~5 ms, FIFO worker
~9–10 ms, render worker ~15.3 ms (moving Onett 15–15.5 ms), GPU ~12–13 ms. The render worker is the limiter; 62 % of its
samples are inside the Mali driver (about 330 draws and 7 render passes per frame).

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
| `MELEE_FLIP_LAYOUT_VAO` | off | one VAO per attribute layout; neutral on this driver (see report) |
| `MELEE_FLIP_CONSTANT_RECORDS` | off | per-draw uniform record pipelines; −1 ms GPU, +5 ms render worker on this driver (see report) |
| `MELEE_FLIP_PRESENT_BLIT` | off | blit instead of the present copy pass; no gain, ±1 scanout pixels |
| `MELEE_FLIP_SORT_OPAQUE` | off | sort opaque depth-ordered runs by state in the direct path (−0.5–0.7 ms, exact on frozen Onett, order risk elsewhere) |
| `MELEE_FLIP_TEXTURE_PAIRS` | off | texture-bank batching; measured slower (see report) |
| `MELEE_FLIP_DAWN_TIMING`, `MELEE_FLIP_DRAW_TRACE`, `MELEE_FLIP_GPU_TEST`, `MELEE_FLIP_SKIP_POINTS`, `MELEE_FLIP_MAPPED_CHECK`, `MELEE_FLIP_MAPPED_VERIFY_GPU`, `MELEE_FLIP_MAPPED_MIRROR`, `MELEE_FLIP_MAPPED_BIND_DAWN` | off | diagnostics only |

Earlier experiments (`MELEE_FLIP_STREAM_UPLOAD`, `MELEE_FLIP_NATIVE_SPECIALIZED`,
`MELEE_FLIP_VERTEX_CACHE`, `MELEE_FLIP_MAP_UPLOAD`, `MELEE_FLIP_DECODE_DIRECT`,
`MELEE_FLIP_RELEASE_TEXOBJ`) remain opt-in and were not re-evaluated.
`MELEE_FLIP_PREWARM_PIPELINES=1` currently crashes at startup and must stay off.

## Validation

- Frozen Onett EFB capture `da46d4a79a8f…` matches the v77/v79 reference for every
  default-configuration build v80–v102 (async included). The scanout capture
  changed at v99b (`eaf1f2147d79…`, 20 pixels differ by ±1); the EFB is the
  criterion.
- Moving Battlefield and moving Onett 60-second soaks complete without GL errors
  or asserts; memory is stable after the FIFO compaction fix (v97).
- `melee_flip_vertex_test` passes on the device with new mixed-format cases;
  `test_flip_prepare.py`, `test_flip_launcher.py`, `test_flip_deploy.py` pass.
- Both dependency patches apply to their pristine sources
  (`git apply --check` against the extracted Dawn archive and a stashed Aurora
  checkout).

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

1. **Render worker below ~14 ms** (moving Onett sits at 15–16 ms: main pass ~9 ms
   of driver time for ~340 draws; the two shadow passes plus their copy/conversion
   passes ~3 ms). Measured and rejected: per-draw uniform records (+5 ms), one VAO
   per layout (neutral). The driver's cost tracks draws and passes, not GL calls,
   so what remains is fusing the shadow passes into one (plus one conversion pass)
   and cutting draws through texture-bank merging of resident geometry. Candidates: blit-based EFB copies instead of
   conversion passes, a swapchain texture pool in `SwapChainEGL` (Dawn allocates a
   fresh texture each frame), fusing the two pre-copy shadow passes, sorting opaque
   draws by program/texture, reducing the ~7 Dawn pass setups.
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
