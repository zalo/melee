Melee Native — Miyoo Flip V2 experimental build

This ARM64 build runs on the tested Surwish/Buildroot installation. It includes
no ROM and does not replace firmware or system libraries. The optional g29p1
GLES driver is loaded privately by the game. Its licence and source information
are under licenses/.

This build renders through upstream-style Aurora (the Miyoo Flip PR set:
CPU vertex decoding, resident display lists, asynchronous frames, stable
texture identities, texture arrays/atlas, render-pass fusion) on Dawn's
standard GLES backend. It carries none of the Flip-specific direct GLES
path, so it is expected to run slower than the v143 build (about 30 FPS on
Onett); it exists to measure that difference. See native/FLIP_AURORA_PRS.md.

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
MELEE_FLIP_ASYNC_FIFO=0, MELEE_FLIP_RESIDENT_DL=0, MELEE_FLIP_TEXTURE_ATLAS=0,
MELEE_FLIP_FUSE_PASSES=0 and MELEE_FLIP_TEXTURE_VERIFY_INTERVAL=N switch the
renderer options for A/B trials. MELEE_FLIP_ASYNC_PRESENT=1 defers the DRM
page-flip wait to the next frame. MELEE_FLIP_DRIVER=g13 selects the installed
driver (untested with this build). Without the optional driver library, the
fallback is selected automatically.

Logs: data/state/game.log
Settings: data/config/melee-native
Shader caches: data/cache/g13/ and data/cache/g29/
