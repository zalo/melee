#!/bin/sh
set -eu
app_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# Keep the tested userspace driver local to this process. System firmware and
# MainUI retain their own graphics stack. Explicit g13 selects the fallback.
driver=${MELEE_FLIP_DRIVER:-auto}
if [ "$driver" = auto ]; then
    if [ -f "$app_dir/lib/mali-g29p1/libmali.so.1" ]; then driver=g29; else driver=g13; fi
fi
case "$driver" in
    g29)
        [ -f "$app_dir/lib/mali-g29p1/libmali.so.1" ] || { echo "Missing bundled g29 driver" >&2; exit 1; }
        export LD_LIBRARY_PATH="$app_dir/lib/mali-g29p1:$app_dir/lib:/usr/lib:/lib"
        export MELEE_FLIP_BARRIER_EVERY="${MELEE_FLIP_BARRIER_EVERY:-0}"
        export MELEE_FLIP_BATCH_DRAWS="${MELEE_FLIP_BATCH_DRAWS:-1}"
        direct_default=0
        if [ "$MELEE_FLIP_BARRIER_EVERY" = 0 ]; then direct_default=5; fi
        export MELEE_FLIP_DIRECT_GLES="${MELEE_FLIP_DIRECT_GLES:-$direct_default}"
        export MELEE_FLIP_ASYNC_PRESENT="${MELEE_FLIP_ASYNC_PRESENT:-1}"
        export MELEE_FLIP_FAST_VALIDATION="${MELEE_FLIP_FAST_VALIDATION:-1}"
        if [ "$MELEE_FLIP_DIRECT_GLES" = 5 ]; then
            export MELEE_FLIP_DIRECT_PACKET="${MELEE_FLIP_DIRECT_PACKET:-1}"
            export MELEE_FLIP_DIRECT_CHECKS="${MELEE_FLIP_DIRECT_CHECKS:-0}"
            export MELEE_FLIP_PRESENT_THREAD="${MELEE_FLIP_PRESENT_THREAD:-1}"
            export MELEE_FLIP_DIRTY_UPLOAD="${MELEE_FLIP_DIRTY_UPLOAD:-1}"
            # Asynchronous frames: the game thread no longer joins the GX
            # translation worker at frame end (see FLIP_HANDOFF.md).
            export MELEE_FLIP_ASYNC_FIFO="${MELEE_FLIP_ASYNC_FIFO:-1}"
        fi
        ;;
    g13)
        export LD_LIBRARY_PATH="$app_dir/lib:/usr/lib:/lib"
        export MELEE_FLIP_BARRIER_EVERY="${MELEE_FLIP_BARRIER_EVERY:-1}"
        export MELEE_FLIP_BATCH_DRAWS="${MELEE_FLIP_BATCH_DRAWS:-0}"
        export MELEE_FLIP_DIRECT_GLES="${MELEE_FLIP_DIRECT_GLES:-0}"
        ;;
    *) echo "Unknown MELEE_FLIP_DRIVER: $driver" >&2; exit 1 ;;
esac
export MELEE_FLIP_VERTEX_INPUT="${MELEE_FLIP_VERTEX_INPUT:-1}"
export MELEE_FLIP_UNIFORM_TABLE="${MELEE_FLIP_UNIFORM_TABLE:-1}"
export XDG_CONFIG_HOME="$app_dir/data/config"
export XDG_CACHE_HOME="${MELEE_FLIP_CACHE_HOME:-$app_dir/data/cache/$driver}"
export MELEE_FLIP_CACHE_HOME="$XDG_CACHE_HOME"
export XDG_STATE_HOME="$app_dir/data/state"
export SDL_AUDIODRIVER=alsa
unset SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS
mkdir -p "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" "$XDG_STATE_HOME"
cd "$app_dir"
# Stock Ports modes can leave two of the four A55 cores offline. Let the
# existing game/FIFO/render/audio/driver workers use all cores while running.
# Preserve the original state and restore it even if the game process crashes.
enabled_cpu_paths=""
game_pid=""
cpu_governor_path=/sys/devices/system/cpu/cpufreq/policy0/scaling_governor
gpu_governor_path=/sys/class/devfreq/fde60000.gpu/governor
dmc_governor_path=/sys/class/devfreq/dmc/governor
cpu_governor_previous=""
gpu_governor_previous=""
dmc_governor_previous=""
restore_cores() {
    if [ -n "$cpu_governor_previous" ]; then printf '%s\n' "$cpu_governor_previous" > "$cpu_governor_path" || true; fi
    if [ -n "$gpu_governor_previous" ]; then printf '%s\n' "$gpu_governor_previous" > "$gpu_governor_path" || true; fi
    if [ -n "$dmc_governor_previous" ]; then printf '%s\n' "$dmc_governor_previous" > "$dmc_governor_path" || true; fi
    for cpu_path in $enabled_cpu_paths; do
        printf '0\n' > "$cpu_path" || true
    done
}
stop_game() {
    if [ -n "$game_pid" ]; then
        kill -TERM "$game_pid" 2>/dev/null || true
        wait "$game_pid" 2>/dev/null || true
    fi
    exit 143
}
trap restore_cores EXIT
trap stop_game INT TERM
# Use supported kernel clock settings while the game runs, then restore the
# firmware's governors on normal exit, game failure, or a launcher signal.
if [ "${MELEE_FLIP_PERFORMANCE:-1}" = 1 ]; then
    if [ -w "$cpu_governor_path" ]; then
        previous=$(cat "$cpu_governor_path")
        if printf 'performance\n' > "$cpu_governor_path"; then cpu_governor_previous=$previous; fi
    fi
    if [ -w "$gpu_governor_path" ]; then
        previous=$(cat "$gpu_governor_path")
        if printf 'performance\n' > "$gpu_governor_path"; then gpu_governor_previous=$previous; fi
    fi
    if [ -w "$dmc_governor_path" ]; then
        previous=$(cat "$dmc_governor_path")
        if printf 'performance\n' > "$dmc_governor_path"; then dmc_governor_previous=$previous; fi
    fi
fi
if [ "${MELEE_FLIP_ALL_CORES:-1}" = 1 ]; then
    for cpu_path in /sys/devices/system/cpu/cpu[0-9]*/online; do
        [ -w "$cpu_path" ] || continue
        if [ "$(cat "$cpu_path")" = 0 ]; then
            if printf '1\n' > "$cpu_path"; then
                enabled_cpu_paths="$enabled_cpu_paths $cpu_path"
            fi
        fi
    done
fi
./melee_native "$app_dir/data/disc.img" >> "$XDG_STATE_HOME/game.log" 2>&1 &
game_pid=$!
wait "$game_pid"
