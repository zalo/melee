> Update: the user identifies the installed OS as Surwish. Earlier “stock”

> Latest result: both stages are deployed. See [the completed backend rewrite and validation](BACKEND_REWRITE.md). Earlier driver defaults, interim failures and proposed work below describe historical investigation.
> references below mean the installed g13p0 vendor stack, not a verified
> official Miyoo firmware image. New testing finds g29p1 passes the tiny
> no-barrier reproduction where g13p0 and g24p0 fail. See
> [MALI_PIPELINE.md](MALI_PIPELINE.md) for current findings.

# Flip barrier investigation: direct reproduction and alternatives

## Reproduction outside Aurora/Dawn

`native/tools/flip_gl_hazards.c` draws a grid of 64 triangles for 30 frames per
case into an offscreen framebuffer. Positions and per-draw offsets are uploaded
once and remain immutable. It compares conventional vertex attributes with
R32UI integer-texture vertex fetching, and changes how transform offsets are
supplied. Each case is repeated with and without per-draw texture-fetch barriers.
Shader setup has no GL errors. Readback validates all 64 triangle centers.

| Vertex input | Transform data | Without per-draw barrier |
| --- | --- | --- |
| Conventional vertex attributes | Ordinary scalar/vector uniforms | Pass |
| Integer-texture vertex fetching | Ordinary scalar/vector uniforms | Pass |
| Integer-texture vertex fetching | Changing ranges in one UBO | DATA_INVALID_FAULT; GL_OUT_OF_MEMORY reported |
| Conventional vertex attributes | Changing ranges in one UBO | Pass |
| Integer-texture vertex fetching | Separate immutable UBOs, switching bindings | Same failure |
| Integer-texture vertex fetching | One fixed UBO array, scalar uniform selects entry | Pass |

Adding the texture-fetch barrier makes the failing controls pass. Switching the
program off/on between draws does not fix them. A separately loaded vendor
library with Rockchip integration revision 11 reproduces the failure; stock is
revision 3. Both use the same Arm g13p0 core revision. System libraries were
not changed. This is a small, valid-API reproduction independent of the port's
GX translation, threading, ROM, or large resource pools.

The identical direct test also passes every case, including no-barrier cases,
on the workstation with NVIDIA ES 3.2 (610.57.04) and Mesa 26.2.1 llvmpipe
(LLVM 22.1.8). These are independent desktop controls, not Mesa running on the
Flip. Logs are in `root-cause/{nvidia,mesa}-control.log`.

The strongest supported diagnosis is a driver-path failure involving
integer-texture vertex fetching together with changing UBO bindings/ranges.
It is not evidence that all draws require explicit synchronization. The exact
internal driver/hardware defect remains unknown. The GL_OUT_OF_MEMORY error
appears with this tiny workload alongside kernel job faults; it should not be
confused with the separate sustained-game RAM exhaustion.

Khronos documents that read-only data does not need MemoryBarrier and that API
updates such as BufferSubData perform required cache invalidation implicitly.
The texture-fetch bit specifically makes preceding shader writes visible to
subsequent fetches. These controls perform no shader writes to vertex/transform
data. The barrier is therefore a workaround, not the expected data dependency
of this workload. [Khronos reference](https://raw.githubusercontent.com/KhronosGroup/OpenGL-Refpages/main/es3.1/glMemoryBarrier.xml).

## Actual-game instrumentation and measurement correction

A one-frame trace shows successive real-game draws reusing a uniform buffer at
changing offsets while fetching vertex data through textures. For example,
several consecutive Battlefield draws use program 132 and UBO 3 with offsets
3456, 5184, 6912, and 8640. This is consistent with the smaller reproduction;
it does not identify an individual hardware descriptor that is corrupt.

The kernel log and `/proc/uptime` use offset clocks on this firmware. A marker
written directly into `/dev/kmsg` was timestamped 4326.646 while uptime was
4313.15. Earlier timestamp-filtered positive fault counts cannot establish
which individual mask caused a fault. New tests use unique kernel markers and
log order, inspect image hashes, and revalidate combined masks. Configuration
changes now drain preceding GPU work before acknowledgement so faulting jobs
from a prior policy are less likely to contaminate the next test.

An opt-in `MELEE_TEST_FREEZE_AFTER` setting freezes simulation updates while
retaining the real game's drawing callbacks. This is intentional fixed-scene
validation, not a new display hang. It also avoids reliance on a pause command
that can be ignored during the starting countdown. Tests retain a RAM watchdog.
Old diagnostic binaries were removed from tmpfs and the alternate library was
moved to the SD card to reduce testing overhead; this does not resolve the
separately observed longer-run memory-growth problem.

## Backend and rendering options

- **Fixed uniform bindings / batching:** the direct test passes when transforms
  are stored in a fixed UBO array and selected by a scalar uniform. Applying
  this to Aurora needs handling for variable-sized uniform records, finite
  uniform-block sizes, shader variants, and transitions between buffer ranges.
  It is a concrete path to reducing barriers without relying on draw ordinals.
- **Conventional vertex attributes:** the direct control passes even with
  changing UBO ranges. A GX backend could expand independently indexed GX
  attributes into ordinary interleaved vertices. That trades CPU conversion
  and upload work for simpler GPU input; performance in this game is unmeasured.
- **Plain-uniform prototype:** `flip_plain_uniforms.cpp` rewrites GLSL uniform
  blocks into ordinary uniform arrays. With per-draw barriers disabled, the
  seven-phase, eight-triangle probe passes with exactly matching hashes and
  no new GPU faults. This prototype reads buffers back to the CPU, adding
  synchronization; it is diagnostic evidence, not a ready performance fix.
- **Stock Vulkan:** no installed loader/ICD was found, and the stock library
  exports no Vulkan instance entry points. The inspected later g13p0 no-CL and
  full GBM libraries also expose none. Aurora's Vulkan backend alone cannot
  supply the missing GPU driver.
- **Mesa Panfrost/PanVK:** Mesa documents G52-family support, including GLES and
  Vulkan, with conformance caveats. The actual device has proprietary `/dev/mali0`;
  its DRM nodes belong to rockchip display and RKNPU, and no Panfrost module is
  available. A compatible kernel/firmware stack would be needed for the standard
  Mesa path. It has not been installed or benchmarked here.
  [Mesa documentation](https://docs.mesa3d.org/drivers/panfrost.html).
- **Experimental PanVK over kbase:** a public G52 project is investigating this
  route, but its own report still describes raw-job DATA_INVALID_FAULT failures.
  It does not establish a working replacement for the Flip.
  [Project report](https://github.com/LukeValen/panvk-mali-g52).

The vendor comparison library was obtained from the maintained Rockchip mirror,
verified against its Git blob hash, and used only with a process-local library
path. [Vendor mirror](https://github.com/JeffyCN/mirrors/tree/libmali).

## Corrected systematic removal result

The marker-based sweep with GPU-draining policy transitions tested the first
64 draw barriers in a fixed Battlefield scene. It retained 31 removals with
mask `0xfe2ffe282a955508`, and the final combined 15-second validation had zero
GPU fault records and an exact panel-image match. Failed cumulative checks
revoked previously accepted removals automatically. See
[results](marked-battlefield-v2/results.json) and
[combined validation](marked-battlefield-v2/validation.json).

This is scene-specific evidence. Draw ordinals change with scene composition;
the mask is not a safe global default. It also supersedes the earlier
clock-filtered claim about draw 32. No performance gain is established by
this correctness sweep.

The fixed-window prototype `flip_uniform_windows.cpp` avoids CPU readback:
it keeps a 16 KiB UBO window bound and selects records through a scalar uniform,
skipping redundant range bindings. Its initial version passes the seven-phase
probe with matching images and no GPU faults, but fails real Onett gameplay
with black output and 2,310 fault records. Its apparent speed is invalid as a
performance result. This confirms that the small reproduction guides the
investigation but does not cover every state interaction in the real game.

The retained mask subsequently failed in moving Onett gameplay. The kernel
ring was flooded with faults and overwrote the start marker; saved records
cannot supply an exact per-trial count. The candidate is archived for the
fixed scene only and is not enabled in normal builds.

A broader window-prototype revision also synchronizes shader, texture, sampler,
and resource-update transitions. It completes the Onett capture with **zero
kernel GPU faults**, but its EFB image does not match the control: the large
“Go!” overlay and character imagery present in the control are absent. Thus
zero kernel faults alone is insufficient. This revision is also rejected for
normal use. It logs GL_INVALID_OPERATION during shutdown. The measured means
(178 ms versus 239 ms for 130 match samples each) cannot establish a valid
speedup because the images differ. The full-barrier control completed with
zero GPU faults. See [comparison](window-game/results.json) and captures.

All live overrides were cleared after testing. The device returned to MainUI
with CPUs 0–1 restored; the installed normal bundle retains all draw barriers
and neither interposer is loaded by its launcher. Deployment used only native
binaries and support files, never the ROM. The native disc, deployment, and
launcher checks passed (3/3), as did Python/shell syntax and diff checks.
