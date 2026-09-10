# Barrier isolation on the Flip

**Follow-up correction:** the stock firmware has an offset between kernel-log
timestamps and `/proc/uptime` (13.5 seconds in one direct check). Historical
positive fault counts below are not reliable attribution to a specific short
mask window. In particular, the earlier draw-32 claim needs marker-based
revalidation. New tests write unique markers into `/dev/kmsg` and identify
faults by log order. See [root-cause investigation](ROOT_CAUSE.md).

The conservative default keeps every per-draw texture-fetch barrier. Diagnostic
masks are not production optimizations: a draw ordinal can mean different
geometry/materials in different frames.

## Full game, fixed input and seed

All trials used Fox/Mario on Onett, four cores, the installed ROM, and a capture
after 120 match frames. The all-barrier control and a separate recovery run
produced identical SHA-256 image hashes and zero new DATA_INVALID_FAULT messages.
The user independently observed black geometry in the first removal trial;
the panel capture confirms it.

| Removed barriers within first 64 draws of each frame | New GPU faults | Mean match frame ms | Exact EFB match |
| --- | ---: | ---: | --- |
| None, control | 0 | 238.10 | Yes |
| All 64 | 501 | 228.71 | No |
| Seeded random 32 | 303 | 239.68 | No |
| First half of that random set (16) | 235 | 238.65 | No |
| Other half (16) | 336 | 238.56 | No |
| Independent random 8 | 329 | 239.79 | No |

Each run completed its capture and restored CPUs to 0–1. Fault counts cover
startup through shutdown, not just the captured match. These are short screen
checks, not sustained-play certification or statistically controlled speed
benchmarks. The approximately 4% improvement from removing all 64 barriers is
invalid as an optimization because it corrupts rendering. None of the sampled
random subsets passed. This does not establish that every individual barrier
is necessary, or that failures are monotonic as barriers are removed.

Exact masks and draw numbers are in [plan.json](game-barriers/plan.json);
[results.json](game-barriers/results.json) records image hashes, timings,
fault counts, and completion. Neighboring files preserve the logs and input.
[Control panel image](game-barriers/control.png) and
[failed panel image](game-barriers/remove1-64.png) illustrate the regression.

## Synthetic probe result

The two-scene-triangle workload makes four GL draws including renderer utility
work. A mask keeping only its first barrier passed all seven phases with exact
images and no new faults. The eight-scene-triangle workload makes ten GL draws;
keeping the first seven barriers passed an isolated full-length run (60 frames
per phase), matching its all-barrier control with no new faults. This does not
prove seven is minimal or generalize to gameplay. Rapid short trials sometimes
returned matching EFB images but GPU faults, so isolated recovery controls are
required. Cold shader-cache short-run failures also require full-length repeats.

## Runtime switching

See [FLIP.md](../../FLIP.md#barrier-diagnostics) and
`native/tools/set_flip_barriers.py`. The running render worker reads a tiny
configuration file at frame boundaries. Atomic replacement prevents partial
reads. Invalid values retain the previous configuration; deletion restores
startup settings. Acknowledgements include the frame number. Default settings
remain all barriers, and experimental files are removed after testing.

## Live single-barrier isolation

All 18 windows ran in the same game process (PID 8618). The match was paused
following 150 retraces; the camera continued settling during early windows.
Each setting had a one-second transition allowance followed by five seconds
of timing and a panel capture, with an all-barrier window after each removal.
Counts below cover each measured window; zero faults is not proof that no
transition fault occurred outside that window.

The old timestamp filter attributed **14 GPU fault records** to removing
draw 32. This attribution is withdrawn because the clocks differ; the
following all-barrier window had zero under that same unreliable filter. Removals of
13, 35, 48, 56, 57, 58, and 61 had zero faults in their measured windows.
Early captures differ even between controls because the paused camera was
still settling; they cannot certify those early candidates visually.

Once the view settled, removing 56, 57, 58, or 61 individually produced exactly
the same panel hash as the neighboring all-barrier captures, with zero faults.
These are candidates for further isolation, not a verified combined mask or
safe removals in other scenes. Timing differences were small; no meaningful
performance improvement is established by these five-second windows.

The renderer acknowledged every live change without restarting, rejected a
malformed file while retaining settings, and restored startup defaults after
file deletion. Full barriers were restored and the game stopped after testing;
MainUI and CPUs 0–1 were verified. The ROM was never transferred.
See [live results](live-barriers/results.json) and neighboring logs/captures.

## Corrected sweep and cross-scene rejection

With unique kernel markers, GPU-draining configuration changes, five-second
candidates and automatic revocation of failing combinations, the first 64
barriers were swept in a frozen Battlefield scene. Mask
`0xfe2ffe282a955508` retained 31 removals and passed the final 15-second
combined image/fault check.

The same mask then produced repeated GPU faults during moving Onett gameplay.
The kernel ring overflowed its start marker, so the saved 3,182 fault records
are not a complete attributable count. The mask is rejected for general use;
normal builds retain full barriers. The previous isolated draw-32 claim is
not supported by the corrected test. See [ROOT_CAUSE.md](ROOT_CAUSE.md),
[corrected sweep](marked-battlefield-v2/results.json), and `cross-scene/`.
