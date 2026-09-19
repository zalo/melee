#!/bin/bash

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source $controlfolder/control.txt
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

# Variables
GAMEDIR="/$directory/ports/melee"
cd "$GAMEDIR" || exit 1

> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

# The user supplies a Super Smash Bros. Melee (US 1.02) disc image; the game reads all four formats.
shopt -s nullglob nocaseglob
discs=("$GAMEDIR"/assets/*.iso "$GAMEDIR"/assets/*.gcm "$GAMEDIR"/assets/*.ciso "$GAMEDIR"/assets/*.rvz)
shopt -u nocaseglob
if [ ${#discs[@]} -eq 0 ]; then
  pm_message "No Melee disc image found in melee/assets. See README.md."
  sleep 15
  exit 1
fi

# Memory card, logs and pipeline cache stay inside the port directory.
export XDG_CONFIG_HOME="$GAMEDIR/runtime/config"
export XDG_STATE_HOME="$GAMEDIR/runtime/state"
export XDG_CACHE_HOME="$GAMEDIR/runtime/cache"
mkdir -p "$XDG_CONFIG_HOME" "$XDG_STATE_HOME" "$XDG_CACHE_HOME"

# Linux caps one environment string at 128 KiB; a whole controller database would stop every command from starting.
if [ ${#sdl_controllerconfig} -lt 100000 ]; then
  export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
fi
chmod +x "$GAMEDIR/melee.aarch64"

# The game links SDL 3 as a shared library; libs.aarch64 holds only the SDL3-over-SDL2 shim (the library
# Dusklight ships), whose "sdl2" driver runs display, audio and pads through this CFW's own SDL 2.
export LD_LIBRARY_PATH="$GAMEDIR/libs.${DEVICE_ARCH}:$LD_LIBRARY_PATH"
# SDL_VIDEODRIVER / SDL_AUDIODRIVER from the CFW name SDL 2 drivers (ROCKNIX: wayland, pulseaudio). Hand
# them to the inner SDL 2 unchanged and point SDL 3 itself at the shim's driver.
if [ -n "${SDL_VIDEODRIVER:-}" ] && [ -z "${SDL3SHIM_SDL2_VIDEODRIVER:-}" ]; then
  export SDL3SHIM_SDL2_VIDEODRIVER="$SDL_VIDEODRIVER"
fi
if [ -n "${SDL_AUDIODRIVER:-}" ] && [ -z "${SDL3SHIM_SDL2_AUDIODRIVER:-}" ]; then
  export SDL3SHIM_SDL2_AUDIODRIVER="$SDL_AUDIODRIVER"
fi
export SDL_VIDEODRIVER=sdl2 SDL_AUDIODRIVER=sdl2

# The game reads the pad through SDL; gptokeyb2 only provides the exit hotkey (melee.ini maps no keys).
$GPTOKEYB2 "melee.aarch64" -c "$GAMEDIR/melee.ini" &

pm_platform_helper "$GAMEDIR/melee.aarch64"
./melee.aarch64 "${discs[0]}"
status=$?

# Display or GPU setup failed: say which instead of returning to the menu silently. The game reaches
# the screen through SDL's KMSDRM or Wayland driver; a CFW offering neither (fbdev-only) cannot run it.
if [ $status -ne 0 ] && grep -q "^\[flip-display\] Cannot initialize SDL video" "$GAMEDIR/log.txt"; then
  pm_message "Melee could not open the display: no KMSDRM or Wayland video driver worked here. Details are in melee/log.txt."
  sleep 15
elif [ $status -ne 0 ] && grep -q "^\[flip-display\] Cannot\|^\[flip-display\] .*needs the KMSDRM" "$GAMEDIR/log.txt"; then
  pm_message "Melee could not start the GPU. It needs an OpenGL ES 3.1 driver (Mali-G31/G52 or newer). Details are in melee/log.txt."
  sleep 15
fi

pm_finish
