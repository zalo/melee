Melee Native — experimental Linux x86-64 port

Run ./melee-native and select your Super Smash Bros. Melee US 1.02 disc
(GALE01 revision 2, ISO/GCM/CISO/RVZ). All game assets, including both font
atlases, are read from that image at runtime. Keep the image accessible.
No emulator, Wine, Proton, game image, extracted assets or firmware is included.

Requires Linux x86-64, glibc 2.39 or newer, a hardware Vulkan driver supported
by Dawn, and an X11 or Wayland desktop. SDL3 supports PulseAudio/PipeWire's
PulseAudio service or ALSA. Wayland file selection requires a working
xdg-desktop-portal file chooser backend (GTK on GNOME/Hyprland).
Ubuntu 24.04 is the validation host; Arch/Omarchy packaging is provided,
not evidence of testing on Omarchy. See native/LINUX.md in the source.

On the validation ThinkPad, Intel UHD620 was faster than the default MX150/NVK.
To use the tested Intel driver on Ubuntu:
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/intel_icd.json ./melee-native
Full short-match coverage used Intel; NVK passed a control match at about 42 FPS.

Keyboard (focus the game window): WASD move, X attack/confirm, Z special/back,
C/V jump, Q/E shield, R grab, IJKL C-stick, Return start/pause.
At the initial save prompt choose No. Saving remains unverified/broken.
Adventure/target stages, exhaustive Kirby copies, moves, effects, costumes
and modes are not certified. Do not assume a locked 60 FPS.

Logs for graphical launch: $XDG_STATE_HOME/melee-native/game.log
(default ~/.local/state/melee-native/game.log).
Config: $XDG_CONFIG_HOME/melee-native (default ~/.config/melee-native).
Cache: $XDG_CACHE_HOME/melee-native (default ~/.cache/melee-native).
Command-line launches write logs to the terminal. Canceling selection exits.

Original recovered game source: https://github.com/doldecomp/melee
Native fork: https://github.com/jonrosner/melee-native
Aurora: https://github.com/encounter/aurora
This software uses FreeType (https://freetype.org).
See Third Party Notices. Recovered game code remains in the executable;
absence of external assets does not establish redistribution rights.
