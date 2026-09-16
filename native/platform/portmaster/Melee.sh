#!/bin/bash
# PortMaster launcher for the native Super Smash Bros. Melee port (melee.aarch64).
# Layout follows the Multiverse Dusklight port. Everything the game writes stays
# under $GAMEDIR/runtime; no system library or firmware file is touched.

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

# Keep the previous launch's log next to the current one.
cp -f "$GAMEDIR/log.txt" "$GAMEDIR/log.prev.txt" 2>/dev/null
> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

mkdir -p "$GAMEDIR/assets" "$GAMEDIR/runtime"

# The user supplies a Super Smash Bros. Melee (US 1.02) disc image in assets/.
# .iso/.gcm are raw dumps; .ciso and .rvz are read directly by the game's disc
# reader (nod), so nothing has to be converted. MELEE_PM_DISC overrides.
disc="${MELEE_PM_DISC:-}"
if [ -z "$disc" ]; then
  shopt -s nullglob nocaseglob
  discs=("$GAMEDIR"/assets/*.iso "$GAMEDIR"/assets/*.gcm "$GAMEDIR"/assets/*.ciso "$GAMEDIR"/assets/*.rvz)
  shopt -u nullglob nocaseglob
  if [ ${#discs[@]} -eq 0 ]; then
    pm_message "No Melee disc image found in melee/assets. Copy your own US 1.02 dump (.iso, .gcm, .ciso or .rvz) there. See README.md."
    sleep 15
    exit 1
  fi
  disc="${discs[0]}"
fi
echo "Disc image: $disc"

# Config (memory card as a Dolphin GCI folder), state (game log) and the
# pipeline cache live under the port directory instead of $HOME.
export XDG_CONFIG_HOME="$GAMEDIR/runtime/config"
export XDG_STATE_HOME="$GAMEDIR/runtime/state"
# One pipeline-cache directory per binary: cached pipeline configs from another
# build would decode to shaders this build does not expect. The stamp records
# the binary the cache belongs to; a newer melee.aarch64 wipes it.
CACHE_DIR="$GAMEDIR/runtime/cache/pipeline"
export XDG_CACHE_HOME="${MELEE_FLIP_CACHE_HOME:-$CACHE_DIR}"
export MELEE_FLIP_CACHE_HOME="$XDG_CACHE_HOME"
if [ -d "$CACHE_DIR" ] && [ "$GAMEDIR/melee.aarch64" -nt "$CACHE_DIR/.binary-stamp" ]; then
  echo "New Melee build detected - clearing the stale pipeline cache."
  rm -rf "$CACHE_DIR"
fi
mkdir -p "$XDG_CONFIG_HOME" "$XDG_STATE_HOME" "$XDG_CACHE_HOME" "$CACHE_DIR"
touch -r "$GAMEDIR/melee.aarch64" "$CACHE_DIR/.binary-stamp" 2>/dev/null

# GPU driver. The CFW's own GLES driver is the default. The optional bundled
# Mali g29p1 blob (lib/mali-g29p1) is only known to work on RK3566 (Miyoo Flip,
# RG353 class); use it there when present. MELEE_PM_DRIVER=system|bundled overrides.
driver="${MELEE_PM_DRIVER:-auto}"
if [ "$driver" = auto ]; then
  driver=system
  if [ -f "$GAMEDIR/lib/mali-g29p1/libmali.so.1" ]; then
    compatible=$(tr '\0' ' ' < /proc/device-tree/compatible 2>/dev/null)
    case "$compatible" in
      *rk3566*) driver=bundled ;;
    esac
  fi
fi
case "$driver" in
  bundled)
    [ -f "$GAMEDIR/lib/mali-g29p1/libmali.so.1" ] || { echo "MELEE_PM_DRIVER=bundled but lib/mali-g29p1/libmali.so.1 is missing" >&2; exit 1; }
    export LD_LIBRARY_PATH="$GAMEDIR/lib/mali-g29p1:$GAMEDIR/libs.${DEVICE_ARCH}:$LD_LIBRARY_PATH"
    ;;
  system)
    export LD_LIBRARY_PATH="$GAMEDIR/libs.${DEVICE_ARCH}:$LD_LIBRARY_PATH"
    ;;
  *) echo "Unknown MELEE_PM_DRIVER: $driver" >&2; exit 1 ;;
esac
echo "GPU driver: $driver"

# Display path: the binary defaults to letting the CFW's SDL own the display (KMSDRM: DRM master
# handoff, console release, page flips) on every device, which is the only path that works on
# handhelds whose kernels our direct DRM/KMS code cannot take over (Anbernic RG351x/AmberELEC,
# etc.). The panel orientation is data, not a code path: we only tell the binary how far to rotate.
# rk3326 handhelds (RG351P/M/V and relatives) mount the panel in portrait, so rotate 270; the Miyoo
# Flip (rk3566) and most others are already landscape (0). MELEE_FLIP_DISPLAY=drm is the opt-in
# escape hatch to the legacy direct DRM/GBM path. Anything the user exported before launch wins.
if [ -z "${MELEE_FLIP_ROTATE:-}" ]; then
  panel_compatible=$(tr '\0' ' ' < /proc/device-tree/compatible 2>/dev/null)
  case "$panel_compatible" in
    *rk3326*) export MELEE_FLIP_ROTATE=270 ;;
    *)        export MELEE_FLIP_ROTATE=0 ;;
  esac
fi

# Renderer defaults of the GLES fast path (presentation worker and asynchronous
# page flips). The SDL display path requires the presentation worker. Any MELEE_FLIP_* the
# user exported before launch passes through untouched; see README.md for the list.
export MELEE_FLIP_PRESENT_THREAD="${MELEE_FLIP_PRESENT_THREAD:-1}"
export MELEE_FLIP_ASYNC_PRESENT="${MELEE_FLIP_ASYNC_PRESENT:-1}"
# MELEE_PM_SWAP_CONTROLS=0 keeps Aurora's stock pad mapping (stick moves, bottom
# face button is A); the default swaps D-pad/stick and A/B for Nintendo-labelled
# handhelds. Same switch as the Flip build's MELEE_FLIP_SWAP_CONTROLS.
[ -n "${MELEE_PM_SWAP_CONTROLS:-}" ] && export MELEE_FLIP_SWAP_CONTROLS="$MELEE_PM_SWAP_CONTROLS"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-alsa}"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
unset SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS

# Pin CPU/GPU/DMC governors to performance for the session and bring every CPU
# core online (some CFW power profiles leave cores offline). Generic sysfs
# globs, not device-specific paths; nodes without a performance governor are
# left alone. Everything is restored on exit, including crashes and signals.
GOV_NODES=()
GOV_PREV=()
ENABLED_CPUS=()
game_pid=""
if [ "${MELEE_PM_PERFORMANCE:-1}" = 1 ]; then
  for g in /sys/devices/system/cpu/cpufreq/policy*/scaling_governor \
           /sys/class/devfreq/*/governor; do
    [ -f "$g" ] || continue
    case "$g" in
      */cpufreq/*) avail="${g%/*}/scaling_available_governors" ;;
      *)           avail="${g%/*}/available_governors" ;;
    esac
    if [ -r "$avail" ] && ! grep -qw performance "$avail"; then
      continue
    fi
    prev="$(cat "$g" 2>/dev/null)"
    [ "$prev" = "performance" ] && continue
    if $ESUDO sh -c "echo performance > '$g'" 2>/dev/null; then
      GOV_NODES+=("$g")
      GOV_PREV+=("$prev")
    fi
  done
fi
if [ "${MELEE_PM_ALL_CORES:-1}" = 1 ]; then
  for cpu in /sys/devices/system/cpu/cpu[0-9]*/online; do
    [ -f "$cpu" ] || continue
    if [ "$(cat "$cpu" 2>/dev/null)" = 0 ]; then
      if $ESUDO sh -c "echo 1 > '$cpu'" 2>/dev/null; then
        ENABLED_CPUS+=("$cpu")
      fi
    fi
  done
fi

restore_system() {
  local i
  for i in "${!GOV_NODES[@]}"; do
    [ -n "${GOV_PREV[$i]}" ] && \
      $ESUDO sh -c "echo '${GOV_PREV[$i]}' > '${GOV_NODES[$i]}'" 2>/dev/null
  done
  for i in "${ENABLED_CPUS[@]}"; do
    $ESUDO sh -c "echo 0 > '$i'" 2>/dev/null
  done
}
stop_game() {
  if [ -n "$game_pid" ]; then
    kill -TERM "$game_pid" 2>/dev/null
    wait "$game_pid" 2>/dev/null
  fi
  exit 143
}
trap restore_system EXIT
trap stop_game INT TERM

# Start+Select exits through gptokeyb's hotkey; the game reads the pad itself.
$GPTOKEYB "melee.aarch64" >/dev/null 2>&1 &

pm_platform_helper "$GAMEDIR/melee.aarch64" > /dev/null

./melee.aarch64 "$disc" &
game_pid=$!
wait "$game_pid"
status=$?
game_pid=""
echo "melee.aarch64 exited with status $status"

$ESUDO kill -9 $(pidof gptokeyb2) 2>/dev/null
$ESUDO kill -9 $(pidof gptokeyb) 2>/dev/null
pm_finish
exit $status
