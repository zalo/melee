# Full-speed investigation — in progress

Latest iteration: [direct GLES, native specialization, and CPU/upload changes](DIRECT_GLES_ITERATION.md).
The historical v9–v20 observations below are retained as evidence; current
launcher settings and timings are documented in that newer report.

User requested continued iteration toward full speed, authorizing system rewrites.
**Full speed has not been achieved.** The canonical `dist/flip/Melee-Native-Flip`
remains the prior release-v8 fallback. Experimental builds and uniquely named
real-game trials are retained separately. No ROM transfers or firmware updates.

## New evidence

- Linux `perf_event_open` user-IP sampling is available on the device. The new
  `native/tools/flip_cpu_sample.c` samples all current game threads and optionally
  records the selected AArch64 registers (currently built with those registers).
- In the release-v8 steady scene, about 8,300 samples were in a Mali memory-copy
  loop at library offset `0x34bc58`–`0x34bc74`. Disassembly shows paired NEON
  loads/stores, not a spin loop or shader compilation. The nearest exported
  symbol is unrelated; the closed library's internal functions are stripped.
- Grouping immutable geometry uploads before all frame passes substantially
  reduces that copying. `perf-v10-preupload` matches the release-v8 EFB exactly
  and has zero new GPU faults. Mean match time is 70.48 ms versus 106.71 ms in
  the new instrumented baseline. Individual settled samples reach ~50 ms, but
  the complete last-50 mean is 69.90 ms: isolated tail samples overstate gains.
- Grouping does **not** cure g13: `perf-v11-g13-none` produces 454 new kernel
  GPU fault/error lines and a different image. It is rejected.
- Keeping staging in CPU memory, uploading only used regions, and shrinking
  the texture staging GPU buffers preserves the image and raises available
  memory to roughly 393,256 KiB at the end of `perf-v12-cpu-upload`. Speed gains
  beyond grouping are small.
- Context-local GL state shadowing (`perf-v19-gl-cache`) did not improve speed.
  Its experimental header is archived as `staged-backend/gl_state_cache_rejected.hpp`
  and removed from the build. It also needs stronger handling of context
  migration, parameter aliases, and generic-buffer binding side effects before
  reuse; do not promote it based on the passing single capture.
- CPU frequencies sampled during gameplay vary from 816 MHz through 1.99 GHz.
  GPU frequency also varies. Governor-controlled comparisons are underway.

## Current source changes after v8

1. Per-thread CPU time alongside wall submit/draw/swap profiling.
2. Immutable geometry uploaded once, before executing any frame passes.
3. CPU staging with `WriteBuffer` of used ranges; texture payload staging stays
   ordered through the existing render operations. Old GPU mapping remains a
   diagnostic fallback (`MELEE_FLIP_CPU_UPLOAD=0`).
4. Bulk attribute decoding, with reference fallback for NBT; numeric and packed
   color differential tests. Prepared input configuration avoids duplicate work.
5. Bounded decoded-geometry cache: source bytes and referenced indexed-array
   spans are hashed, including default matrix and input format. In-place source
   changes alter the key. Cached geometry excludes each draw's uniform-record
   metadata; that metadata is inserted into a separate upload copy. Cap: 16 MiB
   and 2,048 entries. `MELEE_FLIP_VERTEX_CACHE=0` disables it.
6. Idle GX texture-object wrappers expire after 60 frames on Flip instead of 600;
   the content cache and GPU reference ownership remain intact.
7. Canonical pipeline keys omit raw GX input storage format details once CPU
   decoding has converted them to conventional attributes.
8. Separate physical uniform window buffers (diagnostic fallback:
   `MELEE_FLIP_UNIFORM_WINDOWS=0`). A 16 KiB window experiment is current; its
   batching/speed tradeoff has not been accepted over the prior 64 KiB windows.
9. Optional asynchronous DRM page flips retain prior buffers until completion;
   optional EGL interval zero avoids a second pacing mechanism. Both remain
   explicit test settings pending broader validation.
10. Optional Dawn `skip_validation` and `disable_robustness` diagnostics. The
    latter has not demonstrated a speed win and is not a launcher default.

## Trial scope and outstanding work

All successful g29 Onett trials through v20-small-ubo match EFB SHA-256
`da46d4a79a8fe422ac0b68a12162b0ae032566cf031c693647e2b5151bf431b5`
and have zero new kernel GPU faults. This does not qualify every stage or
moving gameplay. Longer Onett controls with async presentation average about
50 ms/frame, well short of the 16.7 ms target. Capture work and first-use shader
compilation cause outliers; use complete logs, not a few favorable lines.

New CLI flags: `--env MELEE_FLIP_NAME=value`, `--cpu-performance`,
`--gpu-performance`. Governor changes are reverted by the trial's exit trap.
The shell harness now records CPU/GPU frequencies and GPU load in `.sensors`.

Pending: finish governor comparisons and render-thread profiling; remove or
refine ineffective experiments; validate changed-stage and moving-gameplay
captures; rerun fixtures/probe/launcher checks; regenerate reproducible patches
and a final package. Preserve the v8 fallback. Do not claim full speed yet.

Sampling notes: retain the matching unstripped executable for symbolization.
`build/flip-tools/perf-v20-symbols` matches v20. Earlier v15 native symbol reports
made after relinking are unreliable; raw IPs/maps and Mali-library offsets are
still valid. The initial v9 reports were produced against the matching binary.

Relevant primary precedent:
[Arm: efficiently updating dynamic resources](https://developer.arm.com/community/arm-community-blogs/b/mobile-graphics-and-gaming-blog/posts/mali-performance-6-efficiently-updating-dynamic-resources)
explains how queued draws constrain buffer updates. This supports testing upload
lifetime changes, but does not establish the proprietary driver's internal fault.
