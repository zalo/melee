Melee Native — Miyoo Flip V2 experimental build

This ARM64 build runs on the tested Surwish/Buildroot installation. It includes
no ROM and does not replace firmware or system libraries. The optional g29p1
GLES driver is loaded privately by the game. Its licence and source information
are under licenses/.

The renderer uses compact decoded vertex buffers, stable parameter tables,
and adjacent compatible draw batches. The g29 path uses direct GLES submission
for supported passes without the old per-draw barrier, with changed-range
uploads and a separate presentation worker. Controlled frozen test windows
average 30.1 ms on Onett and 19.7 ms on Battlefield after a single screenshot.
This is not a 60 FPS release. See
native/validation/2026-09-09-flip/PIPELINED_PRESENT_ITERATION.md for validation limits.

Install in /mnt/SDCARD/Ports/melee-native. Keep the existing verified Melee US
1.02 image at data/disc.img. RVZ works directly; do not transfer it again when
updating the program. Run launch.sh or PORTS > Melee Native.

D-pad moves (it acts as the control stick), right face button (A) confirms
and attacks, bottom face button (B) cancels and does specials, the other two
face buttons jump. The left analog stick acts as the GameCube D-pad (taunt),
right stick is C-stick, R1 grabs, L2/R2 shield, Start pauses. Select + Start
exits. Set MELEE_FLIP_SWAP_CONTROLS=0 before launch.sh for the stock mapping
(stick moves, bottom button is A). Answer Yes at the initial
memory-card prompt; progress is saved to
data/config/melee-native/USA/Card A/01-GALE-SuperSmashBros0110290334.gci
(Dolphin GCI-folder format; copy the folder to back it up).

The launcher enables all four CPU cores and selects supported performance
CPU, GPU, and memory governors while running, restoring the prior settings on exit.
MELEE_FLIP_ALL_CORES=0 and MELEE_FLIP_PERFORMANCE=0 opt out respectively.
MELEE_FLIP_DIRECT_GLES=0 selects reference submission.
MELEE_FLIP_PRESENT_THREAD=0 restores synchronous EGL presentation.
MELEE_FLIP_DIRTY_UPLOAD=0 disables changed-range upload caching.
MELEE_FLIP_DRIVER=g13 selects the installed-driver fallback with barriers.
Without the optional driver library, the fallback is selected automatically.

Logs: data/state/game.log
Settings: data/config/melee-native
Shader caches: data/cache/g13/ and data/cache/g29/
