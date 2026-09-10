# Renderer iteration toward 60 FPS, 2026-09-10 (v79–v112)

This report continues the Miyoo Flip work described in the earlier reports under
`../2026-09-09-flip/`. It records what was measured, what changed in the renderer,
what each change bought, and what remains. Device: Miyoo Flip V2 (RK3566, Mali-G52,
Surwish firmware), app-local Mali g29p1 GLES driver, 640×480 panel.

Frozen Onett (`flip_backend_trial.py`, stage 9, freeze after 45 ticks, capture frame
120) is the controlled reference throughout. Every step below that claims
"bit-exact" produced the same EFB capture (`sha256 da46d4a79a8f…`) and the same
physical scanout capture (`3f2016a8522d…`) as the v77/v79 baseline.

## Result

| Build | Scene | Mode | Mean ms | Median ms | Presented FPS | EFB exact |
| --- | --- | --- | ---: | ---: | ---: | --- |
| v79 (handoff state) | frozen Onett | sync | 30.7 | 30.1 | 32.7 | reference |
| v80 loaders | frozen Onett | sync | 27.2 | 26.5 | | yes |
| v83 resident geometry | frozen Onett | sync | 25.7 | 25.0 | | yes |
| v85 stable texture ids | frozen Onett | sync | 22.6 | 21.8 | | yes (after mode0 fix) |
| v90 mapped uniforms | frozen Onett | sync | 24.5 | 23.7 | | yes |
| v92 mapped uniforms+indices | frozen Onett | sync | 20.9 | 19.9 | | yes |
| v95 record stream | frozen Onett | sync | 19.3 | 18.8 | | yes |
| v95 + FBO cache | frozen Onett | sync | 17.4 | 16.7 | | yes |
| v96 | frozen Onett | async | 17.7 | 17.0 | 56.0 | yes |
| v97 | frozen Onett | async | 17.4 | 16.8 | 55.8 | yes |
| v96 | moving Battlefield | async | 17.6 | 16.7 | 56.9 | n/a (moving) |
| v98 | moving Battlefield | async | 20.2 | 16.7 | 50.0 | n/a |
| v98 | moving Onett | async | 38.5 | 25.8 | 24.5 | n/a |
| v99b | frozen Onett | async | 17.7 | 17.2 | 57.0 | yes |
| v100 invalidate index | moving Onett | async | 27.5 | 26.4 | 34–36 | n/a |
| v100 | moving Battlefield | async | 21.2 | 21.6 | 44–47 | n/a |
| v102 mapped vertices | frozen Onett | async | 17.4 | 16.7 | 57.6 | yes |
| v102 | moving Onett | async | 17.5 | 16.7 (p95 20.3) | 52–59 | n/a |
| v102 | moving Battlefield | async | 17.1 | 16.7 (p95 16.8) | 59–59 | n/a |
| v106 swapchain pool | frozen Onett | async | | render worker 15.3 ms | 58.4 | yes |
| v106 | moving Onett | async | 17.2 | 16.7 (p95 19.5) | 58–59 | n/a |
| v108 line/point loaders | frozen Onett | async | | render worker 15.3 ms | 58 | yes |
| v108 | moving Onett | async | 17.4 | 16.7 (p95 20.3) | 57–59 | n/a |
| v108 | moving Battlefield | async | 17.0 | 16.7 (p95 16.8) | 58–59 | n/a |
| v108 | moving Pokémon Stadium | async | 17.7 | 16.7 (p95 22.6) | 56–59 | n/a |
| v106 | moving Fountain of Dreams | async | 47.2 | 45.7 | 21 | n/a |
| v108 | moving Fountain of Dreams | async | 26.2 | 24.7 (p95 39.3) | 37–40 | n/a (frozen EFB unchanged) |

Mean/median are the presentation intervals from `[flip-thread-present]`
(`analyze_flip_profile.py --tail 300`); FPS is the game thread's `[perf]` line
(ranges exclude the match-start and capture windows).

Frozen Onett and moving Battlefield now sit at the 60 Hz boundary: median frames
are 16.7–17.0 ms with the game thread idle-waiting part of the time. Moving Onett
is still slow. Its tail (p95 ≥ 60 ms through v99) was first attributed to shader
compilation; `pipeline_wait_ms` (v99) disproved that (≤0.2 ms per frame). A CPU
sample of the FIFO worker on its own symbols showed 43 % of the worker inside
`resident::invalidate`, a linear scan of every resident entry and its 26 array
pointers for each released vertex buffer region (effect archives churn constantly
in moving Onett). v100 indexes entries by pointer so a release is a range query.
That exposed the real moving-scene cost: the render worker spent 9–10 ms per frame
in its upload phase, all of it in a few kilobytes of `WriteBuffer` into the shared
vertex buffer. Dawn's GL backend turns that into `glBufferSubData`, and the Mali
driver waits for the previous frame's GPU work before touching a buffer it is
still reading (frozen frames wrote nothing, so they never paid it). v102 records
the streamed vertices into the fenced persistently mapped slots too, and every
scene measured now sits at the 60 Hz boundary with sub-21 ms p95.

## Measurement additions

- `[perf-breakdown]` (game thread, printed with `[perf]` when `MELEE_FLIP_PROFILE=1`):
  per presented frame, game-thread time outside VI (simulation + GX recording),
  time in `aurora_end_frame`, time blocked in the FIFO join (`drain_wait_ms`),
  60 Hz sleep, `begin_frame` slot wait, and the *busy* time of the FIFO worker and
  render worker on their own threads (`fifo_busy_ms`, `render_busy_ms`), plus the
  time the FIFO worker spent blocked on pipeline compilation (`pipeline_wait_ms`).
  Implemented in `native/vi_runtime.cpp`, `lib/gx/fifo.cpp`, `lib/gfx/render_worker.cpp`,
  `lib/gfx/pipeline_cache.cpp`.
- `[flip-render-phase]` / `[flip-render-callback]`: render-worker end-of-frame
  split into upload, plan preparation, texture acquisition, encoding, submit,
  present (`lib/gfx/frame.cpp`, `lib/aurora.cpp`).
- `[flip-dawn-pass]`, `[flip-dawn-submit]` (`MELEE_FLIP_DAWN_TIMING=1`, Dawn patch):
  wall time per labelled render pass and Dawn's submit overhead outside command
  execution.
- `[flip-resident]`: resident geometry cache hits/misses/bytes; `[flip-efb-copy]`:
  unique EFB copy rectangles/formats.
- `native/tools/flip_gl_bench.c`: a standalone surfaceless GLES micro-benchmark
  for per-call driver costs (build with the cross `cc`, run under the g29 library
  path with the CPU governor set to `performance`).
- CPU samples are only meaningful against the binary that produced them; the
  matching unstripped executables are kept as `build/flip-tools/melee_native-vNN-symbols`.

## What the profile said

Baseline (v79) per 30.6 ms frame: game thread 6.5 ms of simulation and GX
recording, then 22.4 ms blocked joining the FIFO translation worker. FIFO worker
busy 28.6 ms, render worker busy 25.1 ms, GPU 11.3 ms for the main pass. The
frame was therefore *serialized* game + translation, with the render worker only
slightly shorter, and the GPU not limiting.

FIFO worker samples (v79): vertex decoding and its memsets ≈ 35 %, texture
hashing ≈ 10 %, pipeline configuration ≈ 6 %, uniform building ≈ 4 %, draw
bookkeeping ≈ 10 %. Render worker samples (v93): Mali driver 62 %, libc 13 %,
direct submission code 9 %, Dawn 7.7 %. Dawn's own overhead was therefore
already small; the GLES driver's per-draw and per-pass cost is what remains.

Micro-benchmark (performance governor, 3000 iterations): plain draw 6–8 µs;
`glBindBufferRange` at a new offset +1–2 µs (earlier run +5); `glBindTexture`
+4 µs, two textures +7; `glBindSampler` +2–3; `glUseProgram` +5–10;
`glTexParameteri` +1 (earlier run +8); `glUniform1uiv` +0.4; `glDrawRangeElements`
about 1.5 µs slower than `glDrawElements`. The two runs differ because the first
ran under the idle governor.

## Changes

All Flip-only, in the Aurora patch unless noted. Each has an environment switch
so it can be A/B tested with the trial tool.

1. **Specialized vertex loaders** (`lib/gx/flip_vertex.hpp`, `MELEE_FLIP_LOADERS`).
   One decode plan per attribute configuration (cached by XXH3 of the attribute
   config), monomorphic per-attribute conversion routines with hoisted invariants,
   every output byte written exactly once (no memset), batch record bits written
   in place, decoding straight into frame storage. The differential test
   (`melee_flip_vertex_test`) gained mixed-format and record-word cases. FIFO
   28.6 → 24.4 ms.

2. **Resident display-list geometry** (`lib/gx/flip_resident.hpp`,
   `GX_AURORA_CALL_DL`, `GX_AURORA_INVALIDATE_RESIDENT`, `MELEE_FLIP_RESIDENT_DL`,
   `MELEE_FLIP_RESIDENT_MB`). `GXCallDisplayList` now emits a reference instead of
   copying the list into the FIFO. The processor decodes each (list, vertex
   format, array identities, matrix slot) once into a per-stride GPU arena
   (`wgpu::Buffer` uploaded on the render worker before the pass that uses it) and
   later calls only append 32-bit indices. Lists are validated per call by a cheap
   hash of their first/last 64 bytes and length; archive regions are invalidated
   through the GX stream when `MeleeNativeUnregisterVertexBuffer` releases them.
   Onett: 447 list calls per frame, all resident after warm-up, about 2 MB
   resident. FIFO → 16.4 ms; vertex uploads shrink from ~3.8 MB to a few hundred KB
   per frame. Per-vertex uniform records for resident draws come from a per-frame
   record stream (vertex slot 1, location 15, `MELEE_FLIP_RESIDENT_RECORDS`) so
   resident draws merge across uniform records exactly like streamed geometry;
   an entry drawn twice in one frame with different records falls back to passing
   its record through the immediates.

3. **Stable texture identities** (`lib/dolphin/gx/GXTexture.cpp`,
   `lib/gx/texture.cpp`, `MELEE_FLIP_STABLE_TEXID`, `MELEE_FLIP_TEXTURE_VERIFY`).
   HSD builds a temporary `GXTexObj` per material per frame, so counter identities
   never repeated and every load re-hashed the whole image (~4 MB/frame). The
   identity is now derived from the image description including `mode0`
   (wrap/filter/bias), `mode1` (LOD), flags and TLUT slot; object-cache hits are
   verified with a sampled content hash (full for ≤ 2 KB, 32 spread samples + tail
   otherwise). Leaving `mode0` out produced a 35-pixel HUD difference because the
   bound-texture cache then kept stale sampler state. FIFO → 13.6 ms. The
   main-thread object-cache sweep also shrank.

4. **Persistently mapped uniform and index streams** (`lib/gfx/frame.cpp`
   `FlipMappedSlot`, `MELEE_FLIP_MAPPED_STREAM`, `MELEE_FLIP_MAPPED_USE`). The FIFO
   worker records uniform records and indices directly into `GL_EXT_buffer_storage`
   mappings (one slot per staging slot, recycled behind a fence created after the
   frame's submit; slot release waits at most one frame). The direct path binds
   those GL names; only frames with a reference-renderer pass copy them into
   Dawn's buffers. Render upload phase 4.1 → 0.2 ms. Two Mali specifics were
   learned the hard way: coherent mappings written from another thread were not
   the problem (bind-time checks and a GPU read-back compare both matched), but
   **index data consumed from a persistently mapped element buffer produced stale
   draws with `glDrawElements`; `glDrawRangeElements` with explicit vertex ranges
   renders correctly** (`MELEE_FLIP_RANGE_ELEMENTS`). Uniform windows are 128 KiB
   Dawn buffers (one window of slack).

5. **Direct GL path trimming** (`lib/gfx/flip_gles.cpp`): redundant fixed-function
   state filtering (`MELEE_FLIP_STATE_CACHE`), texture parameter memo persisting
   across passes/frames and purged through Dawn's destroyed-texture drain, sampler
   state folded into texture objects (`MELEE_FLIP_TEXTURE_SAMPLER_STATE`), uniform
   record selection for resident draws through `glUniform1uiv` instead of a UBO
   rebind, resolved texture units cached per bind group (dropped on bind-group
   cache eviction), plain `glDrawElements`/`glDrawRangeElements` for single
   instances, minimal Dawn resource recording for direct passes
   (`MELEE_FLIP_DIRECT_RECORD_MIN`), and no intermediate present resample pass when
   the EFB already matches the viewport (`MELEE_FLIP_DIRECT_PRESENT`).

6. **Framebuffer object cache in Dawn** (Dawn patch, `CommandBufferGL.cpp`,
   `MELEE_FLIP_FBO_CACHE`, default on). Render passes reuse FBOs keyed by
   attachment GL names instead of gen/attach/check/delete per pass; entries are
   dropped when a texture is destroyed. Destroy notifications are queued from any
   thread and purged on the render thread; calling Dawn's `GetGL()` from another
   thread caused `EGL_BAD_ACCESS` crashes and must be avoided. About 2 ms/frame.

7. **Asynchronous frames** (`MELEE_FLIP_ASYNC_FIFO`, launcher default 1 for g29).
   `aurora_begin_frame`/`aurora_end_frame` no longer join the FIFO worker. The
   frame slot is reserved on the game thread; `GX_AURORA_FRAME_BEGIN(slot)` and
   `GX_AURORA_FRAME_END` travel through the stream, and the FIFO worker begins
   recording, finishes the frame, and hands the presentation closure
   (`gfx::defer_end_frame`) to the render worker. `GXWaitDrawDone` waits only for
   the most recent draw-done token (`fifo::mark_draw_done`/`wait_draw_done`), as
   on real hardware. The FIFO buffer is compacted between frames
   (`fifo::recycle`); before that fix a lagging worker made it grow without bound
   (moving Onett lost 600 MB in 44 s). Readbacks for captures are queued behind the
   frame end (`fifo::run_after_frame`). With this the frame time becomes the
   maximum of the three workers instead of game + translation.

8. **Pipeline-state memo** (`lib/gx/command_processor.cpp`,
   `MELEE_FLIP_PIPELINE_MEMO`): an XXH3 of the raw pipeline-relevant GX state
   selects a cached config/shader-info/pipeline-ref, skipping
   `populate_pipeline_config`, `build_shader_info` and the canonical hash for the
   ~460 dirty-pipeline events per frame that re-send identical materials.
   FIFO 11.7 → 9.9 ms.

9. **Dawn glue** (Dawn patch): `GetDirectGLDestroyedTextures`, submit and per-pass
   timing under `MELEE_FLIP_DAWN_TIMING`, FBO cache; the previously lost direct-GL
   hooks were recovered by the parallel workspace and are unchanged.

10. **Resident invalidation index** (`lib/gx/flip_resident.hpp`, v100). Entries are
    indexed by every pointer they depend on (display list and array bases) in a
    `std::multimap`; `MeleeNativeUnregisterVertexBuffer` releases became a range
    query instead of a scan of all entries × 26 arrays. This was 43 % of the FIFO
    worker in moving Onett and the source of its 60–120 ms tail frames.

11. **Mapped vertex stream** (`lib/gfx/frame.cpp`, `flip_gles.cpp`, `encoding.cpp`,
    `MELEE_FLIP_MAPPED_VERTICES`, default on; `MELEE_FLIP_MAPPED_USE=all` now covers
    vertices). Each fenced slot owns a persistently mapped GL vertex buffer that the
    FIFO worker decodes into directly; the direct path binds it, Dawn's vertex
    buffer is refreshed whole only when a pass falls back to the reference
    renderer. Removes the 9–10 ms implicit-sync stall described above:
    moving Onett 27 → 17.5 ms mean, Battlefield 21 → 17.1 ms. Diagnostics:
    `[flip-upload-phase]` splits the render-worker upload phase.

12. **Swapchain texture pool** (Dawn patch `SwapChainEGL.cpp`/`TextureGL.cpp`,
    `native/platform/flip/present_worker.cpp`, `MELEE_FLIP_SWAPCHAIN_POOL`, default
    on). The presenter returns consumed textures to a pool that the swapchain wraps
    (`DirectGLPresentCallbacks::acquire`) instead of allocating a texture per frame;
    released handles keep their framebuffer-cache entries. Acquire 0.29 → 0.12 ms,
    present pass 0.8 → 0.6 ms; render worker 16.3 → 15.3 ms frozen, 16–17 → 15–15.5
    ms moving Onett.

13. **Resident arena uploads as GPU copies** (`flip_resident.hpp` `directUpload`,
    `flip_gles.cpp`, `MELEE_FLIP_RESIDENT_COPY`, default on). Cache misses used to
    `WriteBuffer` into an arena the GPU was reading (the same implicit sync as
    item 11, ~4 times per second in moving Onett). The bytes now go into a fresh
    staging buffer and `glCopyBufferSubData` on the render thread before submit.

14. **Specialized line/point expansion** (`flip_vertex.hpp`, v108). GX_POINTS and
    GX_LINES went through the reference per-vertex decoder plus a struct-based
    quad expansion. Fountain of Dreams draws ~25,000 point sprites per frame
    (~1,850 GX_POINTS draws inside one display list that the resident cache refuses
    because of the primitive type), which cost the FIFO worker ~30 ms per frame.
    The specialized loader now decodes line/point formats too and the expansion
    copies whole records; records are assembled in local memory because the
    destination is write-combined mapped storage and reading it back on the FIFO
    worker cost ~20 ms per frame on its own. FIFO worker 46 → 23 ms on Fountain,
    frozen Fountain capture `cc2fea00f05f…` unchanged, Onett unchanged. The
    per-vertex record region got a CPU shadow for the same reason.

## Findings worth keeping

- **Other stages (v106/v108, moving matches):** Pokémon Stadium and Hyrule Temple
  present at 16.7 ms median (p95 21–31 ms, the latter from mid-match pipeline
  waits). Fountain of Dreams is GPU-bound: its main pass takes 16–17 ms of GPU
  (`[flip-direct-gpu]`), of which dynamic uniform-record indexing is ~1.5 ms
  (`MELEE_FLIP_GPU_TEST`); the rest is the fill of ~25k blended point sprites.
  After the v108 FIFO fix it runs at ~37 fps. Dropping the sprites
  (`MELEE_FLIP_SKIP_POINTS=1`, diagnostic, wrong image) puts the main pass at
  8.9 ms of GPU and the FIFO worker at 14.6 ms, and the stage still only reaches
  ~48 fps because of its ~490 draws per frame. The water reflection is not an
  EFB-sized copy: `grizumi.c` renders the fighters through a second camera and
  copies an 80×60 RGB565 texture (pass 0, ~150 draws, ~1.2 ms of GPU), so it
  costs draw-call CPU rather than GPU fill. Going further needs GPU-side sprite
  work (fewer fragments per sprite, per-draw constant records) and cheaper draws.
- Fountain of Dreams also hit a game-side assertion twice in seven runs
  (`synth.c:214`, "Can't load SFX file; bank buffer overflow", both times while
  CPU sampling was active); unrelated to rendering, recorded here so it is not
  mistaken for a renderer crash.
- **Never read back from mapped vertex memory on the FIFO worker.** The
  persistently mapped slots are write-combined; a single 4-byte read per record
  in the point expansion cost ~20 ms per frame on Fountain.

- **Per-draw uniform records are a dead end on this driver** (v110/v111,
  `MELEE_FLIP_CONSTANT_RECORDS=1`, opt-in). Two variants gave unmerged draws a
  pipeline whose record index is not a per-vertex value: binding the uniform range
  at the draw's record (v110) and reading the index from the immediates in both
  stages (v111). Both cut the main-pass GPU time only ~1 ms (not the 2.7 ms the
  literal-index diagnostic suggested), moved 137 pixels of the frozen capture by
  one unit (compiler precision), and raised the render worker from 15.3 to
  20.5 ms: on the g29 blob any per-draw uniform change, a `glBindBufferRange` or
  a `glUniform` update alike, costs ~15 µs, as much as the draw itself. The
  vertex-embedded record index therefore stays; the same cost explains the earlier
  constant-attribute dead end. The variant machinery (`FlipConstantRecord`,
  `pipeline_ref_async`, `flip_constant_variant`) remains for experiments.
- **Present blit is not a win** (`MELEE_FLIP_PRESENT_BLIT=1`, v105, opt-in):
  replacing the full-screen copy draw with `glBlitFramebuffer` costs the same
  ~0.8 ms (the pass setup, not the draw, is the cost) and moves 20 scanout
  pixels by one unit. The pass cost was the per-frame swapchain texture (item 12).

- **Never write into a buffer the GPU may still read.** On this Mali driver a
  partial `glBufferSubData` (Dawn `WriteBuffer`) into an in-flight buffer blocks
  the CPU until the previous frame's GPU work finishes, about one GPU frame. Any
  streamed data must go through fenced, persistently mapped storage (Dolphin's
  stream-buffer discipline). The resident arenas still take partial `WriteBuffer`
  uploads on cache misses (~4 per second in moving Onett); if tail frames return,
  that is the first suspect.
- The scanout capture hash changed at v99b (20 pixels differ by ±1 from the
  v79 reference, EFB identical); the EFB hash is the exactness criterion.

- The user-visible Dawn/WebGPU overhead was small once direct submission existed;
  the *architecture* it imposed (translate everything every frame, join the
  translator every frame) was the cost. Resident geometry and asynchronous frames
  address that directly.
- GX only clears the copied rectangle on EFB copies; Aurora clears the whole
  frame at each copy (three full clears per frame, two 256×256 shadow copies in
  Onett). Fusing those passes is possible but invasive; making the clears partial
  saves GPU tile traffic but not CPU. Not done.
- `MELEE_FLIP_GPU_TEST=1` renders with a constant uniform-record index (wrong image)
  and cuts the main pass GPU time from 11.8 to 9.1 ms: dynamic UBO indexing in the
  TEV fragment shaders costs ~2.7 ms of GPU per frame. GPU total is ~13–15 ms per
  frame; this is the GPU headroom lever if it becomes limiting.
- Disabling Dawn robustness made no measurable difference.
- **Main-pass shape (moving Onett, v103 `[flip-gl-calls]`, per frame):** ~338
  draws, ~147 program switches, ~293 texture binds, ~84 vertex-buffer binds, ~39
  attribute-layout changes, ~30 uniform-window binds, ~66 immediate updates. At
  the microbenchmark's per-call costs that is 6–7 ms of the 9 ms the main pass
  takes on the CPU; the Mali driver's draw-time validation is the rest. Resident
  draws fail to merge mostly on texture (~179 per frame) and pipeline (~104);
  `[flip-batch-resident]` reports the first mismatch per draw.
- **State-sorted opaque runs** (`MELEE_FLIP_SORT_OPAQUE=1`, v104, opt-in): the
  direct path stable-sorts runs of consecutive opaque, depth-tested, depth-written
  LESS/LEQUAL draws by pipeline, texture group and uniform window. Only ~79 of
  ~340 draws per frame qualify (the rest blend, skip depth writes, or are split by
  viewport/scissor changes); program switches 147 → 83, texture binds 293 → 220,
  uniform-window binds 30 → 182. Render worker −0.5–0.7 ms, frozen Onett stays
  bit-exact, moving Onett p95 20.3 → 19.4 ms. Left opt-in because coplanar opaque
  surfaces (decals drawn after their base with LEQUAL) would change order on other
  stages.
- The g29p1 Mali blob exports no Vulkan entry points and does not advertise
  `GL_EXT_multi_draw_indirect`; GLES 3.2 with `GL_EXT_buffer_storage`,
  `GL_ARM_shader_framebuffer_fetch` and base-vertex draws is what this device has.
- **Texture-pair batching is a dead end** (`MELEE_FLIP_TEXTURE_PAIRS=1`, v103):
  bit-exact, but the bind-group layout grows to 15 banks and the direct path binds
  every entry per draw (~4,800 texture binds per frame), render worker 17 → 19 ms.
  Streamed merges gained only ~6 draws per frame because streamed geometry is a
  small share of the frame.
- Pipeline prewarming (`MELEE_FLIP_PREWARM_PIPELINES=1`) currently crashes at
  startup (`std::out_of_range` in `map::at` while loading 1257 cached configs) and
  stays off.

## Open issues

1. **Moving-scene tails were not compile stalls.** `pipeline_wait_ms` in
   `[perf-breakdown]` (v99) shows ≤0.2 ms per frame of pipeline-creation waits on a
   warm cache (2–6 waits per 120 frames), so the 35–120 ms p95 frames through v99
   came from the FIFO worker itself: `resident::invalidate` scanned all entries per
   released region (fixed in v100, see below). Compile stalls still exist on a cold
   `dawn_cache.db`; Dolphin-style non-blocking creation or repaired prewarming
   remain the answer for first-run matches.
2. **Render worker at ~17 ms.** Remaining per frame (Onett): main pass ~8 ms of
   driver time for ~330 draws, two 256×256 shadow passes and their two conversion
   passes ~2.5 ms, present pass 0.45 ms, Dawn pass setup ~0.3 ms × 7, plan
   preparation 0.8 ms, encode 0.35 ms. Ideas not yet done: blit-based EFB copies
   (loses 4-bit quantization), a swapchain texture pool (Dawn allocates a new
   texture per frame), fusing the pre-copy passes, sorting opaque draws by
   program/texture (changes draw order semantics).
3. **Moving Onett** is render-bound at ~17.5 ms (render worker 17.7 ms with ~340
   draws and the two shadow copies); the FIFO worker is at ~10 ms and the game
   thread idles ~3 ms per frame. Item 2 is what remains.
4. Game logic itself is ~5–6 ms per simulated tick on this CPU; it doubles when
   the pad queue catches up after slow frames.

## Reproduce

Build/deploy/measure as in [FLIP.md](../../FLIP.md). Typical trials:

```sh
python3 native/tools/flip_backend_trial.py my-frozen --batch 1 --soak 20 --env MELEE_FLIP_ASYNC_FIFO=1
python3 native/tools/flip_backend_trial.py my-battle --batch 1 --stage 31 --freeze-after 0 --soak 60 --env MELEE_FLIP_ASYNC_FIFO=1
sha256sum native/validation/2026-09-09-flip/staged-backend/my-frozen.ppm   # da46d4a79a8f… expected
```

Raw evidence for every vNN trial named in this report is under
`native/validation/2026-09-09-flip/staged-backend/` locally (logs, captures,
memory/sensor samples). It is not committed.
