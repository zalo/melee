#!/bin/sh
# Device-side runner: launch the game with an input script for a bounded time,
# then collect the game log and the memory-card folder listing.
# Usage: flip_save_trial.sh <name> <input-script> <seconds> [ENV=VALUE...]
cd /mnt/SDCARD/Ports/melee-native || exit 1
name=$1; script=$2; seconds=$3; shift 3
root=/mnt/SDCARD/Ports/melee-native/data/diagnostics/save-trials
mkdir -p "$root"
for kv in "$@"; do export "$kv"; done
export MELEE_INPUT_SCRIPT="$script" MELEE_TRACE_INPUT=1
cp data/state/game.log "$root/previous.log" 2>/dev/null
: > data/state/game.log
ls -la "data/config/melee-native/USA/Card A" > "$root/$name.card-before" 2>&1
timeout -s TERM -k 10 "$seconds" /bin/sh ./launch.sh
echo "$?" > "$root/$name.status"
cp data/state/game.log "$root/$name.log"
ls -la "data/config/melee-native/USA/Card A" > "$root/$name.card-after" 2>&1
echo done > "$root/$name.done"
