# Staged backend rewrite — September 9, 2026

Work requested: execute the driver-validation stage and the rendering rewrite,
including deeper backend changes when required. Completed both stages: an application-local g29 driver and rewritten GX
vertex/parameter/submission paths. All tests reuse the existing
`data/disc.img`; no ROM upload or firmware installation occurs.

## Driver and rendering findings

- A matched Onett freeze after 45 simulation updates exposes a black EFB on
  g29 with the original integer-texture vertex path, even with barriers. A
  separate empty shader cache does not fix it. The earlier single moving
  capture was insufficient driver acceptance.
- Replacing vertex decoding with conventional CPU-decoded VBO inputs renders
  correctly with barriers on g13. Compared with the original g13 image, 1,011
  of 307,200 pixels change by exactly one 8-bit channel step; maximum delta is
  one. This is a small numerical difference, not byte-exact equivalence.
- g13 still faults without barriers with the new vertex inputs, including
  when using the native uniform table. The small reproduction did not cover
  all whole-game hazards. These failed runs are retained, not accepted.
- g29 plus conventional vertex inputs renders the complete Onett control
  without barriers and without new kernel faults. Barrier-on and barrier-off
  images are byte-identical. Cross-driver images differ slightly; the exact
  synchronization comparison therefore uses the same driver and backend.
- Compact vertex layouts reduce Onett's vertex upload from 6,249,620 bytes to
  1,486,032 bytes and preserve the image exactly.
- Native uniform tables retain the same image: each GX record occupies a 4 KiB
  slot in a 64 KiB UBO window. A WGSL wrapper with `@size(4096)` preserves the
  original generated structure layout. All adapter/required limits are
  checked by Dawn; the Flip requests its advertised 64 KiB binding limit.
- Matched Onett means: original g13 250.95 ms, initial g29 VBO 114.34 ms,
  compact g29 VBO + native table 94.48 ms. These include startup match frames.
  Timing is CPU wall time, not GPU timestamps.
- Battlefield advances 300 simulation updates and captures after 360 rendered
  match frames. g29 compact/table images with and without barriers are
  byte-identical. The no-barrier sample averages 86.46 ms over 364 frames.
- Fountain of Dreams exposes roughly 2,500 draws/frame and about 1 second of
  draw submission. Its first 360-frame diagnostic exceeded the 180-second
  watchdog. This motivates batching adjacent primitives with different
  parameter records; it is not a successful completed replay test.

## Display capture correction

The old `scanout.c` only supported DRM dumb-buffer mapping. g29 presents
imported DMA buffers, so that utility returned `mapdumb: Invalid argument`
and left its previous PPM behind. Earlier g29 `.scanout.ppm` files copied by
the shell harness are therefore not fresh display evidence. They must not be
interpreted as a physically frozen display.

`native/tools/flip_scanout.c` now exports the GEM handle as a dma-buf when
necessary, performs CPU-read synchronization, snapshots it, and atomically
writes the capture. It deletes stale output before trying. A live Fountain
of Dreams scanout successfully showed the correct scene, fighters, water,
HUD, and opening overlay. The guarded trial now records scanout failure
instead of copying a stale file. Later trials validate the updated capture.

## Implemented architecture

The tracked `aurora-flip.patch` carries the source changes. The dependency
checkout is the working source; the final patch has been regenerated and
verified with `git apply --reverse --check`.

- CPU decoding on the existing FIFO worker handles direct/independent indexed
  attributes, endian conversion, fixed-point values, packed colors, NBT/NBT3,
  and line/point expansion. Array reads are bounds checked.
- Conventional compact vertex layouts contain only attributes present in the
  GX draw. Vertex pulling through integer textures is retained as a diagnostic
  fallback. The main path avoids vertex-stage SSBO requirements entirely.
- Original GX material, TEV, depth/blend, viewport/scissor, and EFB pass ordering
  remain in Aurora/Dawn. Uniform tables operate within Dawn's resource model,
  without modifying generated GLSL in an interposer.
- Adjacent batching carries the record index in the vertex stream
  and a flat fragment varying. All vertices of each primitive share its index.
  GLSL compatibility requires `@interpolate(flat, either)`; the first trial
  using implicit `first` was rejected by Dawn validation before rendering.
- Batches require the same pipeline, texture group, destination alpha, fog LUT,
  uniform window, and contiguous geometry. They stop at actual command/pass
  boundaries and the 16-bit vertex-index limit. Existing transparent ordering
  is preserved. No cross-material reordering is introduced.

## Reproduction

`native/tools/flip_backend_trial.py NAME --driver g29 --vertex 1 --table 1
--batch 1 --barriers 0 --stage 9 --freeze-after 45 --capture-frame 120`
performs the guarded MainUI handoff, uses the existing ROM, records a unique
kernel marker and memory samples, and collects EFB/DRM captures. `--soak N`
continues N seconds after the first capture; `--freeze-after 0` keeps moving.

Tests of the compact unbatched path are archived in `staged-backend/`.
The FIFO decoder fixture passes on both the host and the Flip. Launcher tests
cover driver selection, conservative fallback, barrier overrides and CPU
restoration. Runtime deployment continues to reject bundles containing ROMs.

## Final outcome and deployment

Both stages are implemented and deployed in `dist/flip/Melee-Native-Flip` and
`/mnt/SDCARD/Ports/melee-native`. The launcher selects the private g29 library,
compact conventional VBO inputs, native uniform tables, adjacent batching,
and no workaround barrier before every draw. Normal resource/pass
synchronization remains. Aurora/Dawn still provides GX material and render-pass
semantics; replacing that entire framework was unnecessary.

The private g29 library is checksum pinned, packaged with its license, and
loaded only by this application. Surwish's system driver, kernel and firmware
are unchanged. `MELEE_FLIP_DRIVER=g13` selects the installed driver with
conservative per-draw barriers and batching disabled by default. Driver caches
are separate. Explicit runtime overrides remain available. CPU cores enabled
by the launcher return to their previous state on exit.

Final acceptance evidence is in `staged-backend/final-results.json`, alongside
raw logs, memory samples, kernel markers, EFB captures and display captures.

| Trial | Match samples | Mean frame time | Approximate FPS |
| --- | ---: | ---: | ---: |
| Original g13, Onett control | 125 | 250.95 ms | 4.0 |
| Final release, Onett control | 129 | 106.52 ms | 9.4 |
| Final release, moving Onett soak | 1,055 | 97.82 ms | 10.2 |
| Final release, Fountain, 300 simulation updates | 369 | 158.68 ms | 6.3 |

These are CPU wall frame times, including match warmup, rather than pure GPU
timings. Fountain's earlier unbatched run sampled roughly 2,500 draws/frame
and roughly one second/frame, then exceeded the watchdog. Batching reduced
sampled submission to roughly 470 draws in the frozen control and 442 at the
end of the final moving replay. That earlier timeout is not a completed
matched replay benchmark. The completed release remains below full speed.

### Image and fault checks

- Every completed final release trial has zero new kernel GPU faults.
- Onett VBO and Battlefield compact/table barrier-on/off EFB controls match
  byte for byte. Batched Fountain barrier-on/off controls match both EFB and
  fresh physical scanout byte for byte.
- Batching changes only three Battlefield pixels by one channel step versus
  the unbatched image. Onett batching changes 487 pixels by at most one step.
  These small arithmetic differences are reported rather than called exact.
- The final cache/memory changes preserve the accepted batched Onett EFB and
  physical scanout byte for byte. The final moving Fountain scanout visibly
  contains fighters, stage, effects, and HUD after 300 simulation updates.
- Fresh scanout evidence begins with `batch-v7-*`; older g29 scanout artifacts
  are excluded because of the capture bug described above.
- The final graphics probe passes all seven phases, including eight draws in
  multi-draw phases over 30 frames each, with zero new GPU faults.
- Decoder fixtures pass on host and ARM, with a host ASan/UBSan run also passing.
  The cross build completes for game, probe, decoder test and DRM capture tool.
  The native disc, deployment and launcher CTest suites pass (3/3).

### Memory and remaining limits

The Flip now compiles pipelines on demand instead of prewarming the entire
historical cache. First-use compilation blocks through the existing worker
so primitives are not silently omitted. Disk shader caching remains enabled;
`MELEE_FLIP_PREWARM_PIPELINES=1` restores historical prewarming for diagnostics.
The unused legacy vertex texture is reduced to a tiny dummy allocation.

Both extended Onett runs completed about 122 seconds of sampling (90 seconds
after first capture), with no GPU fault or memory watchdog stop. In the fixed
scene, available memory ended at 295,292 KiB, varying by only 984 KiB over the
last 30 seconds; live pipelines remained at 143. Moving gameplay ended at
302,972 KiB available but consumed another 15,040 KiB over the last 30 seconds
as additional pipelines appeared. This demonstrates a fixed-scene plateau,
not bounded memory over arbitrarily long sessions. Longer play and additional
stages remain unqualified.

The retained g13 no-barrier failures show that draw synchronization and driver
behavior were real problems, but they were not the entire bottleneck. Vertex
fetching, parameter updates, draw count and pipeline work also mattered. The
rewrite removes the problematic vertex-pulling path and amortizes compatible
draw submissions; it does not claim a proven internal cause for the closed
Mali driver's faults.

The deployment utility verified the canonical package against the installed
release without transferring any changed files. No ROM was uploaded during
these tests. The device was returned to MainUI with CPUs 0–1 online and no
pending test launch or barrier override file.
