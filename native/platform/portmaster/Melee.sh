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

# On some CFWs (e.g. ROCKNIX on the RG351 series) gptokeyb2 grabs the physical pad and the
# game instead sees gptokeyb2's passthrough device, the "OpenSimHardware OSH PB Controller".
# get_controls only maps the physical pad, so without this the virtual pad has no Start binding
# and the menus become unusable. Append PortMaster's own OSH mapping (falling back to the
# known-good layout) so the game works whether it reads the physical or the virtual pad.
osh_map="$(grep -m1 'OpenSimHardware OSH PB Controller' "$controlfolder/gamecontrollerdb.txt" 2>/dev/null)"
if [ -z "$osh_map" ]; then
  osh_map="03000000091200000031000011010000,OpenSimHardware OSH PB Controller,a:b0,b:b1,back:b7,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,leftshoulder:b4,leftstick:b8,lefttrigger:b10,leftx:a0~,lefty:a1~,rightshoulder:b5,rightstick:b9,righttrigger:b11,rightx:a2,righty:a3,start:b6,x:b2,y:b3,platform:Linux,"
fi
sdl_controllerconfig="${sdl_controllerconfig}"$'\n'"${osh_map}"

# Linux caps one environment string at 128 KiB; a whole controller database would stop every command from starting.
if [ ${#sdl_controllerconfig} -lt 100000 ]; then
  export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
fi
chmod +x "$GAMEDIR/melee.aarch64"

# European translation of the game's own on-screen text (menus, tips, results, trophy
# blurbs). Pick the language, in order of preference: an explicit melee/language.txt, then
# the language chosen in PortMaster (config/config.json "language", e.g. "de_DE" - the same
# value its own GUI uses, so this follows the CFW without a per-CFW special case), then the
# system locale. Anything unset or unsupported keeps the retail US English text. Supported
# codes: de es fr it pt nl pl. The compiled tables ship in melee/locale.
export MELEE_LOCALE_DIR="$GAMEDIR/locale"
if [ -z "${MELEE_LANG:-}" ]; then
  melee_lang_src=""
  if [ -f "$GAMEDIR/language.txt" ]; then
    melee_lang_src="$(head -n1 "$GAMEDIR/language.txt")"
  fi
  if [ -z "$melee_lang_src" ] && [ -f "$controlfolder/config/config.json" ]; then
    melee_lang_src="$(sed -n 's/.*"language"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$controlfolder/config/config.json" | head -n1)"
  fi
  [ -z "$melee_lang_src" ] && melee_lang_src="${LANG:-}"
  case "$(printf '%s' "$melee_lang_src" | tr '[:upper:]' '[:lower:]')" in
    de*) MELEE_LANG=de ;; es*) MELEE_LANG=es ;; fr*) MELEE_LANG=fr ;;
    it*) MELEE_LANG=it ;; pt*) MELEE_LANG=pt ;; nl*) MELEE_LANG=nl ;;
    pl*) MELEE_LANG=pl ;; *) MELEE_LANG="" ;;
  esac
  export MELEE_LANG
fi

# The game links SDL 3 as a shared library; libs.aarch64 holds only the SDL3-over-SDL2 shim (the library
# Dusklight ships), whose "sdl2" driver runs display, audio and pads through this CFW's own SDL 2.
export LD_LIBRARY_PATH="$GAMEDIR/libs.${DEVICE_ARCH}:$LD_LIBRARY_PATH"
# SDL_VIDEODRIVER / SDL_AUDIODRIVER from the CFW name SDL 2 drivers (ROCKNIX: wayland, pulseaudio). Hand
# them to the inner SDL 2 unchanged and point SDL 3 itself at the shim's driver. We do NOT force a driver
# here: display, audio and pads use the CFW's own SDL 2 exactly as it configures itself (its explicit
# hint if set, otherwise its own autodetect). A device that needs a forced driver is handled by the
# KMSDRM retry below, and only when the CFW's own path actually fails to bring up the display.
if [ -n "${SDL_VIDEODRIVER:-}" ] && [ -z "${SDL3SHIM_SDL2_VIDEODRIVER:-}" ]; then
  export SDL3SHIM_SDL2_VIDEODRIVER="$SDL_VIDEODRIVER"
fi
if [ -n "${SDL_AUDIODRIVER:-}" ] && [ -z "${SDL3SHIM_SDL2_AUDIODRIVER:-}" ]; then
  export SDL3SHIM_SDL2_AUDIODRIVER="$SDL_AUDIODRIVER"
fi
export SDL_VIDEODRIVER=sdl2 SDL_AUDIODRIVER=sdl2

# Aurora's texture-fetch-barrier driver probe re-probes whenever a heavier scene appears. On some
# good Mali drivers (measured: Mali-G31 r13p0) it never finds a fault yet keeps re-probing every few
# frames in busy scenes - each probe renders the busiest pass twice with glFinish, which stalls the
# frame and intermittently corrupts what is presented (black flashes over the stage). Force the
# barrier off, which also marks the policy overridden so the probe never runs. A device that truly
# needs the barrier can set AURORA_GLES_DRAW_BARRIER (0/1/pass/finish/flush) itself.
export AURORA_GLES_DRAW_BARRIER="${AURORA_GLES_DRAW_BARRIER:-0}"

# The game reads the pad through SDL; gptokeyb2 only provides the exit hotkey (melee.ini maps no keys).
run_melee() {
  $GPTOKEYB2 "melee.aarch64" -c "$GAMEDIR/melee.ini" &
  local gptokeyb_pid=$!
  pm_platform_helper "$GAMEDIR/melee.aarch64"
  ./melee.aarch64 "${discs[0]}"
  local rc=$?
  kill "$gptokeyb_pid" 2>/dev/null
  return $rc
}

# First launch uses the CFW's own SDL 2 as-is. Some CFWs (dArkOS RE) set no video driver and SDL 2
# autodetect then lands on a GL path our EGL can't see ("SDL did not create an EGL display"). Only in
# that case, and only when nothing already pinned the inner driver, retry once forcing KMSDRM.
run_melee
status=$?
if [ $status -ne 0 ] && [ -z "${SDL3SHIM_SDL2_VIDEODRIVER:-}" ] \
   && grep -q "SDL did not create an EGL display" "$GAMEDIR/log.txt"; then
  export SDL3SHIM_SDL2_VIDEODRIVER=kmsdrm
  run_melee
  status=$?
fi

# Display or GPU setup failed: say which instead of returning to the menu silently. The game reaches
# the screen through SDL's KMSDRM or Wayland driver; a CFW offering neither (fbdev-only) cannot run it.
if [ $status -ne 0 ] && grep -q "^\[flip-display\] Cannot initialize SDL video" "$GAMEDIR/log.txt"; then
  pm_message "Melee could not open the display: no KMSDRM or Wayland video driver worked here. Details are in melee/log.txt."
  sleep 15
elif [ $status -ne 0 ] && grep -q "SDL did not create an EGL display" "$GAMEDIR/log.txt"; then
  pm_message "Melee could not get a GL display through this CFW's SDL 2 (a KMSDRM retry did not help). Please share melee/log.txt and your GPU driver setting."
  sleep 15
elif [ $status -ne 0 ] && grep -q "^\[flip-display\] Cannot\|^\[flip-display\] .*needs the KMSDRM" "$GAMEDIR/log.txt"; then
  pm_message "Melee could not start the GPU. It needs an OpenGL ES 3.1 driver (Mali-G31/G52 or newer). Details are in melee/log.txt."
  sleep 15
elif [ $status -ge 128 ]; then
  # The game was killed by a signal. Distinguish three cases so a normal quit is not reported as a
  # crash: (1) a genuine fault - SIGILL/ABRT/BUS/FPE/SEGV, or any run that logged an abort backtrace
  # (OSPanic / Aurora LOG_FATAL both print one before aborting); (2) an out-of-memory kill (SIGKILL
  # with a matching kernel oom-killer line); (3) the Start+Select exit hotkey, which gptokeyb2
  # delivers as SIGKILL too - that is a clean user quit and must stay silent.
  sig=$((status - 128))
  if [ $status -eq 132 ] || [ $status -eq 134 ] || [ $status -eq 135 ] || [ $status -eq 136 ] || [ $status -eq 139 ] \
     || grep -q "Native game panic\|melee\.aarch64(\|) \[0x[0-9a-f]" "$GAMEDIR/log.txt"; then
    pm_message "Melee crashed (signal $sig). Please share melee/log.txt; the next launch starts with a fresh shader cache."
    sleep 10
  elif [ $status -eq 137 ] && dmesg 2>/dev/null | grep -qiE "out of memory.*melee|oom-kill.*melee|killed process [0-9]+ \(melee"; then
    pm_message "Melee ran low on memory and was closed. Details are in melee/log.txt."
    sleep 10
  fi
  # Otherwise it was the exit hotkey (SIGKILL/SIGTERM) or a clean shutdown: return to the menu quietly.
fi

pm_finish
