# Super Smash Bros. Melee (native) for PortMaster, {{TAG}}

Native AArch64 build of *Super Smash Bros. Melee* (US 1.02) for PortMaster CFWs, built from
[zalo/melee `{{SHORT}}`](https://github.com/zalo/melee/commit/{{COMMIT}}) on {{DATE}}. It contains
no game data: you need your own dump of the disc (US, version 1.02, `GALE01`).

## Changes in this build

- **Fixes the broken graphics on systems with Mesa 25.3 to 26.1.3** (fighters drawn as huge spiky
  polygons, textures missing or scrambled; found and reported by the tester with the Raspberry Pi 5 on
  Batocera 43, Mesa 25.3.6, once the previous build no longer crashed there). It is the same driver bug
  as the start-up crash below: those Mesa releases silently ignore the values the game's shaders are
  given through one kind of uniform array, with no error in the log. The tester found the driver
  setting that avoids it, `disable_uniform_array_resize=true`, and the game now applies it by itself on
  the affected releases, in the same restart that switches the shader cache off (the log says
  `restarting with disable_uniform_array_resize=true MESA_SHADER_CACHE_DISABLE=true`), so nothing has
  to be added to `Melee.sh`. Verified with Mesa 25.3.3 on an emulated system: the previous build draws
  the same garbage there, and this build's frame of a match matches the one from an unaffected Mesa.
  Confirmed by the tester on the Pi 5 with the setting exported by hand; a confirmation with this build
  is welcome.
- **Fixes the crash at start-up on systems with Mesa 25.3 to 26.1.3** (reported from a Raspberry Pi 5 on
  Batocera 43, report `777957ff`: `signal 11` at address `0xffffffffffffffff` inside `libgallium`, right
  after `Using dawn cache` in the log, on every launch after the first; in the previous build already,
  and since confirmed by the tester). Those Mesa releases crash when they load a compiled shader
  program back from a cache, their own or the game's, if the program has the kind of uniform array
  this game's shaders have. It is a driver bug, fixed in Mesa 26.1.4 and 26.2. On the affected releases
  the game starts itself again once, with the driver's shader cache switched off. Shaders are then
  compiled on every launch there, so the first seconds of a stage can stutter more than elsewhere.
- **The frame rate can be shown on screen.** Main menu > Options > Port Settings > **Show Frame Rate**
  puts it in the top right corner (frames drawn a second, refreshed twice a second) and remembers the
  choice. When the game itself falls behind real time, the speed it is running at is shown beside it.
  `MELEE_SHOW_FPS=1` in `Melee.sh` turns it on for one run.
- **Leaving with Start + Select no longer ends in a crash** (`signal 11` after `Device was destroyed` in
  the log; reported on Knulli on the RG34XX-SP and dArkOS on the RG353V). The game shut the renderer
  down piece by piece on the way out and crashed in the middle of it on those devices. It now simply
  ends. Nothing was lost in those crashes: the save is written when the game changes it, not on exit.
  If you had this crash, please tell us whether it is gone.
- **"Melee Soak Test" no longer reports a crash that was not one.** Two things stopped the test with
  `timed out waiting for scene` and `exit status 134`: a notice of something earned in an earlier run,
  which comes up between the title screen and the menu, and a button pressed while the test was
  starting. The CPU player now takes over from wherever the game is. A test left with Start + Select
  is reported as stopped, not as crashed.
- **Crash logs say where the crash was** even when the stack cannot be walked: the `[crash]` lines now
  include the program counter and return address and the libraries they are in.
- **The launcher no longer logs `pm_platform_helper: command not found`** on older PortMaster versions
  (AmberELEC ships 2024.12.31, which has no such function).
- **Fixes the crash on the results screen after a four-player match** (`Memory Empty`, `sislib.c:86` in
  the log). The memory set aside for a screen's text was sized for the GameCube's 32-bit structures;
  the statistics of four players no longer fitted. Two- and three-player matches were not affected.
- **Fixes `SDL did not create an EGL display` at launch on R36S-class devices** (ArkOS and dArkOS
  builds, Mali-G31). On those systems SDL2 is pointed at `libEGL.so`, the Mali driver, while the
  `libEGL.so.1` the game asked for can be a different library that knows nothing about the display SDL
  opened. The game now takes EGL from the same driver that provides OpenGL ES whenever that driver
  contains both. If the message still appears, the log now also names the GLES library and SDL's driver
  settings; please share it.
- **"Melee Soak Test"**: VS and Special Melee matches are now played with two, three and four players in
  turn (the crash above needed four), and the test's self-update no longer loses PortMaster's helper
  functions on the launch right after an update (`pm_message: command not found`, wrong screen size in
  the report).
- **"Melee Soak Test" now sends its report by itself and keeps the port up to date.** The second entry
  in Ports lets a CPU-controlled player play through Classic, Adventure, All-Star, Event matches, the
  Stadium modes, Training, VS and Special Melee for 30 minutes, with rumble off and on a copy of your save
  (records, unlocks and settings stay as they are). Start + Select stops it early.
  - At the end it writes `ports/melee/soak-report.txt.gz` (the log plus the device, CFW, kernel, the
    matches played and the memory use each minute) and, when the device is online, posts that file to
    `https://melee-reports.sels.tech`, the port's report collector. The screen shows the report's id;
    quote it when you describe what you saw. Nothing else is sent, and the collector keeps the sender's
    address only as a salted hash that limits how many reports one device can send per day.
  - Before it starts it checks GitHub for a newer release of this port and installs it (the download is
    verified by its SHA-256), so you always test the latest build. If the new build cannot start, the
    previous one is put back.
  - Without a network the test runs the same: no update, and the report stays on the card for you to
    share. An empty file `ports/melee/soak/offline` keeps the test off the network for good.
  - Only the soak test does any of this. Starting Melee normally never contacts anything.
- **Fixes the crashes and hangs found by running every game mode unattended**, most of them places where
  the 64-bit build read or wrote a GameCube data layout wrongly:
  - Adventure: enemies now appear in the Mushroom Kingdom, the Underground Maze and on Icicle Mountain
    (they were missing), and the Topi, the polar bear, a block hit from below and the falling platforms on
    Icicle Mountain no longer crash. An enemy dropping a trophy overflowed a list on the stack.
  - F-Zero Grand Prix (the route table), the acid on Brinstar (a collision test that produced an invalid
    position) and Event matches that use more than four player slots (Events 37 and 49).
  - Stamina Mode, which stopped at its sound-bank load, and the Tournament menus.
  - Pichu falling asleep ended the game with `Null script branch target`; Samus's Grapple Beam and Kirby's
    aerial jumps read the wrong attributes; a thrown item could leave at the wrong angle; an item hitbox
    with a very large damage value hit an assertion.
  - Sound: a full voice bank and two races between the game and the mixer thread.
- **Small corrections**: the sword-trail colours of Marth, Roy, Link and Young Link, the bonus list on the
  results screen and the Rumble settings menu.
- **Fixes the Start button (and other buttons) on ROCKNIX and CFWs that route the pad through gptokeyb.**
  On devices where the frontend hands the game gptokeyb's virtual controller instead of the physical pad
  (notably ROCKNIX on the RG351 series), the controller had no button mapping, so Start did nothing and the
  menus were unusable. The port now always supplies that virtual controller's mapping, so the pad works
  whether the game reads the physical or the virtual device.
- **Fixes the crash when opening Special Melee.** Entering the Special Melee menu crashed the game on
  every device: that menu has more options than a fixed-size list the menu renderer used, so it wrote past
  the end and corrupted the stack (the extra safety checks in this build turned that into an immediate
  crash). The list is now sized correctly.
- **Fixes the out-of-memory crash on heavy stages (e.g. Onett) on 1 GB handhelds.** The texture cache
  kept far more GPU texture memory resident than its budget implied, growing the working set until the
  system ran out of memory and killed the game (worst on RAM-limited devices like the Miyoo Flip and the
  RG35XX family). The cache now scales to the device's RAM and releases unused textures much sooner, which
  also **greatly improves frame rate** on those stages (a match on Onett went from stuttering and crashing
  to a steady ~35 fps in testing). Tunable with `MELEE_TEXTURE_CACHE_MB` and `MELEE_TEXOBJ_IDLE_FRAMES`.
- **No more flashing textures or black stage floors on Mali GPUs.** On some Mali drivers (notably the
  Mali-G31 in the RG35XX / H700 handhelds) stage geometry and HUD icons would briefly flash the wrong
  texture, and the stage floor could turn solid black. The renderer now identifies textures by their
  contents instead of by a memory address that the game reuses across different textures, so cached
  textures no longer get crossed. This also stops a slow memory leak that could eventually kill the game.
- **Items with hitboxes strike fighters again.** Ray-gun bullets, the Super Mushroom and every other
  script-hitbox item passed straight through fighters instead of hitting them; a byte-order bug that
  dropped the "this hitbox can hit a fighter" flag on little-endian CPUs is fixed.
- **Game & Watch's sausages obey gravity.** Chef's sausages flew flat instead of arcing, because a
  64-bit pointer in the shared item state overwrote the sausage's type index; the layout is fixed so the
  sausages fall as they should.
- **More reliable GPU driver-bug detection.** On Mali GPUs whose driver needs a per-draw workaround, the
  renderer now keeps checking for the rendering bug when a heavier scene loads later (for example Fountain of
  Dreams reached after the menu), instead of only during the first few seconds — so the workaround engages on
  those scenes too. Correct drivers are unaffected.
- **Crash fix on match exit.** Leaving a match (for example exiting training mode) could crash the game
  with a segmentation fault; a stale fighter reference in the per-frame state check is now guarded.
- **Runs on more CPUs.** The build no longer uses the optional ARMv8 crypto instructions, which some
  aarch64 chips (e.g. the Raspberry Pi 4 / Cortex-A72) do not have; those devices previously crashed on
  launch with an illegal instruction. Verified now booting and rendering on Raspberry Pi 4 (V3D).
- **Internal render resolution.** On a high-resolution display (1080p or larger) the 3-D scene now renders
  at quarter resolution and is upscaled on output, which greatly relieves fill/present-bound GPUs at high
  panel resolutions (on a Pi 4 at 1080p a match went from ~10 to ~30 fps); the HUD/output stay full
  resolution and low-resolution handhelds are unaffected. Override with `MELEE_FLIP_RENDER_SCALE`
  (0 auto, 1 native, 2 half, 4 quarter).
- **Opt-in draw-call merge** for stage geometry via `MELEE_FLIP_RESIDENT_RECORDS=1` (off by default; ~+13%
  on Fountain of Dreams on a Mali-G31).
- **Starts on PowerVR GPUs (TrimUI Smart Pro on Knulli/Batocera).** The renderer failed to initialise the
  GPU on drivers that do not expose every EGL entry point through `eglGetProcAddress` (PowerVR, some older
  Mali), fell back to the software path and aborted. It now loads those core EGL functions directly from the
  same driver library, so GPU init succeeds. Mali devices are unaffected.
- **Cleaner reporting when the game hits an internal assertion.** A failed engine assertion used to be
  masked as a segmentation fault at a tiny address (because a GameCube-only crash handler ran on hardware it
  cannot drive here); it now prints the file and line and aborts cleanly, so the log identifies the real
  fault. This changes how such crashes are *reported*, not whether they happen.

- **Fixes the freezes on Mali handhelds (Miyoo Flip, RG351P).** The picture could stop mid-match or on a
  loading screen with the game still running and the sound looping, until the device was restarted. The
  vendor Mali driver postpones texture uploads made from a pixel buffer and then waits on a thread that is
  itself waiting for the upload; the renderer now uploads through a mapped buffer the driver cannot defer,
  and a second lock-order deadlock between the render thread and synchronous shader compilation is gone.
- **Long sessions no longer run out of memory on 1 GB handhelds.** GPU and system memory grew with every
  match until the system killed the game or froze, after 20 to 40 minutes: frames were sent to the GPU
  twice whenever a shader was still compiling, the cached stage and fighter geometry of every earlier match
  was kept, compiled shaders were kept without limit, small textures each reserved a whole texture slab,
  and each chunk of streamed music and every finished match left its disc buffers and converted game files
  behind. All of these are now released or bounded. In unattended testing a CPU-controlled player cleared
  Classic mode six times in a row on the Miyoo Flip (41 minutes, at least 210 MB free throughout) and on the
  RG35XX SP (45 minutes); the RG351P, which used to die after about 20 minutes, ran 45 minutes with 350 MB
  to spare.
- **Fixes a crash on Kongo Jungle when the barrel cannon catches a fighter**, and the same defect on Kongo
  Jungle (N64), Battlefield, Princess Peach's Castle and Venom: a 64-bit pointer inside a 32-bit stage
  parameter block shifted the fields after it, so a stage script could not be found (the log ended with
  `Script branch has no live archive owner`).
- **Fixes the controller doing nothing on the Miyoo Flip under ROCKNIX.** The built-in pad was opened but
  not assigned to a player port, so Start and every other button were dead. A pad that arrives without a
  player slot is now seated on the first free port.
- **Fixes crashes and wrong behaviour in several fighters and stages** found by running Classic, Adventure
  and the stadium modes unattended: Master Hand and Crazy Hand, Pikachu's Thunder, Jigglypuff's costumes
  with a hat, Yoshi's and Kirby's egg, throws, the Super and Poison Mushroom size change, a fighter
  teetering on certain ledges, the Arwings on Corneria and Venom, Race to the Finish, and the credits after
  Classic mode.
- **Saves survive a hard power-off.** After writing the memory card the game now also flushes the
  directory entry, so switching the device off right after a match no longer leaves an empty or corrupt
  save on FAT cards.
- **Crash reports show the real fault address.** Under some CFWs a crash was logged with the game's own
  process id in place of the faulting address.

## Install

1. Download `melee.zip` below. Do not unpack it. (The newest release is always at
   <https://github.com/zalo/melee/releases/latest>, and its zip at
   <https://github.com/zalo/melee/releases/latest/download/melee.zip>; those two links do not change
   from release to release.)
2. Copy it to `ports/PortMaster/autoinstall/` on the card the CFW uses for ports (`roms/ports` on
   AmberELEC, ArkOS and ROCKNIX; `ROMS/ports` on muOS; the `ports` share on Knulli and Batocera), then
   start PortMaster once: it installs the zip and removes it from `autoinstall`. Unpacking the zip
   straight into `ports/` works too.
3. Copy your disc image (`.iso`, `.gcm`, `.ciso` or `.rvz`, see the
   [Dolphin ripping guide](https://wiki.dolphin-emu.org/index.php?title=Ripping_Games)) into
   `ports/melee/assets/`.
4. Refresh the game list, start **Super Smash Bros. Melee (native)** from Ports, and answer **Yes**
   at the memory-card prompt on first boot. Saves live in `ports/melee/runtime/config`.

The first matches after installing stutter while shaders compile; later runs use the pipeline cache
in `ports/melee/runtime/cache`. Full controls, port settings and the developer menu are in the port's
`README.md` (inside the zip; PortMaster shows it from the port's info screen).

| Handheld | Game |
| --- | --- |
| Left stick | Control stick |
| D-pad | D-pad (taunt) |
| Right stick | C-stick |
| A / B | A / B |
| X, Y | Jump |
| R1 | Z (grab) |
| L2 / R2 | L / R (shield) |
| Start | Start |
| Start + Select | Exit |

## What runs it

Display, sound and pads go through the CFW's own SDL 2 (the bundled `libSDL3.so.0` is the
[SDL3-over-SDL2 shim](https://github.com/bmdhacks/SDL/tree/sdl2-backend) Dusklight also ships), so
KMSDRM, fbdev and Wayland CFWs are all fine and nothing on the device is replaced. Rendering needs the
CFW's OpenGL ES 3.1 driver (Mali-G31/G52 and newer, or Panfrost). On a Mali driver that drops draws
(libmali g13p0 on Knulli, g24p0 on ROCKNIX nightlies before about August 2026) the game shows a
"GPU driver update needed" notice and a banner along the bottom edge and runs correctly but slowly.
On ROCKNIX, update to 20260901 or newer (libmali g29p1) or switch the GPU driver setting to Panfrost;
please include the driver line from that notice in a report.

A device that cannot draw 60 frames a second still plays single-player at full speed: the game
simulates every frame and draws fewer of them, as the console does under load. `log.txt`'s `[perf]`
lines show the picture rate (`presented_fps`) and the game rate (`logic_fps`) separately;
`dropped_periods` above zero means the device could not keep up even so. Online matches keep one
drawn frame per game frame on both consoles, so they run at the slower device's frame rate.

## Reporting problems

Attach `ports/melee/log.txt` (written on every launch) and say which device, CFW and GPU driver you
used. If the display cannot be opened, that log lists the video drivers, the `/dev/dri` nodes and
SDL's reason for rejecting each driver. If the game crashes, the log ends with `[crash]` lines that
locate it, and the next launch starts with a fresh shader cache by itself when the crash happened
during start-up.

## Files

| File | Purpose |
| --- | --- |
| `melee.zip` | The PortMaster port ({{SIZE}} bytes, SHA-256 `{{SHA256}}`) |
| `melee-version.txt` | This release's name and the zip's SHA-256, read by the soak test's updater |
| `melee-portmaster-symbols.tar.gz` | Unstripped `melee.aarch64` matching this zip, for crash backtraces |
