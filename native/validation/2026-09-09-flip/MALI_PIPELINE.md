# Mali G52 pipeline and newer-driver investigation

> Latest result: both stages are deployed. See [the completed backend rewrite and validation](BACKEND_REWRITE.md). Earlier driver defaults, interim failures and proposed work below describe historical investigation.

## Firmware identification correction

The user identifies this installation as **Surwish OS**, not stock Miyoo OS.
Earlier references to “stock” in these notes mean the installed vendor graphics
stack unless explicitly comparing firmware images. A Buildroot identity is not
proof of an unmodified stock distribution. Read-only inspection reports kernel
5.10.160 #140 (February 27, 2025), a Miyoo355 Buildroot build stamp, and installed
Arm g13p0 / Rockchip revision 3. No comparison boot on official Miyoo firmware
has been performed, so Surwish-specific causation is unproven.

The idle GPU is 200 MHz under simple_ondemand, with configured limits 200–900 MHz.
During the subsequent g29p1 game control it reached 900 MHz; the idle reading
does not establish underclocking during gameplay. Installed libmali SHA-256 is
`4233c30d930e454f00247638a627fd36f35061fa8f7129c472aab2505593f940`. OS-selected
CPU online state already affected performance in previous measurements. Kernel,
GPU userspace, resource lifetime, and governor behavior must be distinguished
from the distribution name.

## Measured capabilities and driver candidates

`mali-precedents/` contains direct EGL extension and limit queries. On g13p0,
g24p0 and g29p1 GLES:

- ES 3.2; 64 KiB maximum uniform block; 4,096 vertex uniform vectors.
- 16 vertex texture units; **zero vertex SSBOs and zero vertex image uniforms**.
- No advertised multi-draw, multi-draw-indirect, shader-draw-parameters or
  base-instance extension. ES core indirect draws and instancing are distinct
  from multi-draw support.

The zero vertex SSBO limit explains why the Flip port substituted integer
textures for Aurora's usual storage-buffer vertex fetching. It also means a
proposed GLES vertex shader reading a giant SSBO draw table is not implementable
on these drivers as advertised.

The g24p0 revision 10 library from the Rockchip mirror was SHA/Git-blob verified,
loaded only by a small offscreen process, and successfully initialized on the
existing kernel. It still fails direct cases 2 and 4 without barriers, with six
new kernel fault/error records. With barriers those controls pass.

A newer source exists outside the mirror's older `libmali` branch: the Flip
mainline project's g29p1 update pins mirror commit
`1a082323f1001874a007e4e522029d6c46d75ae9`. That package includes Vulkan integration.
The separately downloaded GLES library (g29p1 revision 5) also initializes on
the installed kernel; it prints a missing `large_page_conf` parameter warning.
The complete six-case offscreen reproduction **passes on g29p1**, including
both cases that fail without barriers on g13p0/g24p0. Kernel-marker inspection
shows zero new fault records. This changes the diagnosis: the same kernel and
application sequence can work with a newer userspace driver. It does not yet
establish whole-game or display correctness.
[Flip package change](https://github.com/Zetarancio/distribution/commit/9f57190200).

The mainline Flip project documents a newer kernel/mali_kbase stack as well.
Changing a userspace library and changing the complete OS are different tests.
Its current default is proprietary libmali, so “use ROCKNIX” does not necessarily
mean “use Mesa”. [Device driver documentation](https://raw.githubusercontent.com/Zetarancio/Miyoo-Flip-Mainline-Linux-Reverse-Engineering/main/docs/drivers-and-dts/drivers.md).

Mesa Panfrost is a separate GLES implementation conformant on G52. PanVK lists
G52 Vulkan support but is not conformant there and requires an explicit
experimental opt-in. Standard Mesa needs the Panfrost kernel interface, which
is absent from the current installation. This remains a complete compatible
stack exercise, not a replacement `.so` on the existing `/dev/mali0` API.
[Mesa support matrix](https://docs.mesa3d.org/drivers/panfrost.html).

## Mali precedents that change the design

Arm explicitly recommends batching resource updates before drawing and using
multiple resource versions with fences to avoid reusing data still consumed by
the GPU. Updating a live resource can cause driver copies or pipeline drains.
This is a plausible contributor to our memory growth and submission cost, not
an established explanation: the tiny failing reproduction uses immutable data.
[Arm dynamic-resource guidance](https://developer.arm.com/community/arm-community-blogs/b/mobile-graphics-and-gaming-blog/posts/mali-performance-6-efficiently-updating-dynamic-resources).

Arm recommends ordinary uniforms for small per-draw constants, leaving large
matrix arrays in buffers. Our small controls agree: changing ordinary uniforms
works without the workaround, as does one fixed uniform array with a scalar
record index. The earlier GLSL window interposer's rendering regression does
not disprove that architecture; the interposer changes generated declarations
and GL state outside Dawn's resource model and is not a complete backend.
[Arm application guidance](https://developer.arm.com/community/arm-community-blogs/b/mobile-graphics-and-gaming-blog/posts/mali-performance-5-an-application-s-performance-responsibilities).

The G52's Bifrost geometry pipeline performs position shading before culling,
then computes remaining varyings for surviving geometry. Conventional indexed
vertex inputs with separate position-related and other attribute streams are
a natural fit. Replacing those loads with integer texture fetching changes the
memory path and needs measurement rather than assuming desktop vertex-pulling
advice applies. [Arm Bifrost architecture](https://developer.arm.com/community/arm-community-blogs/b/mobile-graphics-and-gaming-blog/posts/the-mali-gpu-an-abstract-machine-part-4---the-bifrost-shader-core).

Arm also documents shading groups of four contiguous vertex indices, including
indices not explicitly drawn. Indirect loads must safely cover those extra
lanes. Dense indices and padded valid input records matter; putting metadata in
high index bits can inflate intermediate storage. These are audit requirements,
not a diagnosis of this fault. Our tiny texture has padding and its parameter
access is not indexed by those extra vertices. The same presentation identifies
native command streams and native multi-draw-indirect as later CSF-generation
features, so G52 API availability alone would not establish equivalent cost.
[Arm Vulkanised 2023, slides 37–40](https://www.vulkan.org/user/pages/09.events/vulkanised-2023/vulkanised_2023_getting_started_on_mobile_and_best_practices_for_arm_gpus.pdf).

## Proposed pipeline: no per-draw workaround as the target

The first implementation should preserve GX ordering while replacing the
failing input path, then add GPU-generated work where it saves measured time.

1. Record GX commands into segments ending at actual EFB-copy/read dependencies.
   Retain immutable geometry and all parameter records needed by each segment.
   Upload changed data before the segment's draws; maintain bounded, fenced
   resource versions rather than overwriting buffers in flight.
2. Expand GX's independently indexed attributes into dense conventional indexed
   VBOs. Cache static conversions. First use a CPU decoder as a correctness and
   cost baseline; optionally use compute for dynamic decoding/skinning once
   measured. Position and its skinning inputs belong together, with unrelated
   varyings in another stream. Explicitly support points, lines and sprites.
3. Keep large matrix/parameter tables stable within each batch; select records
   using a small uniform or integer vertex/instance attribute. A 64 KiB UBO
   cannot hold every full per-draw record in a complex frame, so size batches
   from real layouts. Fragment tables can use the available fragment SSBO path
   only after separate correctness validation.
4. Merge adjacent compatible primitives, or instance genuinely repeated meshes.
   Preserve shader/TEV behavior, blending, depth, scissor and texture state.
   Transparent draws and EFB effects constrain reordering. Texture arrays are
   an option only for compatible formats, sizes and sampling semantics.
5. If compute produces vertex/index/indirect-command data, issue the appropriate
   memory-barrier bits **once at the producer-to-consumer phase boundary**.
   Draw all compatible batches without an application barrier after each draw.
   Fence allocation reuse at submission/frame boundaries. Real EFB dependencies
   still need ordering; “no per-draw barriers” does not mean no synchronization.

Arm's compute example explicitly uses a vertex-attribute barrier between compute
writing a buffer and rendering from it. This is the expected phase-level model.
[Arm ES developer guide](https://documentation-service.arm.com/static/649ad0f138511951cb799268).

A fully GPU-generated list still needs some CPU submissions on the tested GLES
implementation, because multi-draw is not exposed. The immediate payoff is
stable resource bindings, fewer compatible batches and a vertex path already
passing our direct control. Vulkan g29p1 or PanVK could enable a different
submission backend, but must be probed for actual device features and correctness
before making that the implementation dependency.

Acceptance requires exact fixed-scene captures including overlays, moving
matches on several stages, no new kernel faults or runtime GL errors, bounded
memory over longer runs, and timing with all intended geometry present. No FPS
claim follows from the design alone. Firmware files and the ROM remain unchanged.

## Real-game g29p1 check

The first moving Onett run on g29p1 disabled all in-frame per-draw barriers
(`MELEE_FLIP_BARRIER_EVERY=0`; auxiliary barriers remain). It completed the
capture and recorded zero new GPU faults, but the EFB capture is entirely black.
Its 50.4 ms mean is **not a valid performance result**. The driver improves the
tiny reproduction but is not yet a working game solution. See
[g29 game comparison](g29-game/results.json). Normal launch settings have not
been switched to g29p1.

Restoring the per-draw barriers on g29p1 restores the scene, including the
“Go!” overlay, with zero new GPU faults. The EFB image is not identical to the
earlier g13p0 control: normalized RMSE 0.110826; 4.64% of pixels differ using
ImageMagick's 5% fuzz threshold. Differences need matched replay investigation;
this is neither an exact-image pass nor proof of a driver rendering bug.
The g29p1 full-barrier run averaged 169.54 ms over 133 match samples versus
239.25 ms over 130 earlier control samples. Different draw counts, timing and
images prevent a validated speedup claim. The promising candidate remains
app-local and opt-in for diagnostics. MainUI and CPUs 0–1 were restored.

The practical priority is now a matched replay comparison of g29p1 with the
installed driver, followed by phase/state-based synchronization experiments on
g29p1. The conventional-vertex pipeline remains the architectural fallback.
A full rewrite should not be assumed necessary before this newer-driver path
is evaluated. No newer kernel, firmware or Vulkan stack was installed.
