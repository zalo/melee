#!/bin/sh
cd /mnt/SDCARD/Ports/melee-native || exit 1
name=$(cat /tmp/game-barrier-name)
mask=$(cat /tmp/game-barrier-mask)
base=$(cat /tmp/game-barrier-base)
root=/tmp/game-barrier-bisect
mkdir -p "$root"
export MELEE_FLIP_PROFILE=1 MELEE_TEST_SEED=1 MELEE_INPUT_SCRIPT=/tmp/melee-match-controls.input
export MELEE_MATRIX_TEST=1 MELEE_TEST_FORCE_STAGE=1 MELEE_TEST_STAGE=9 MELEE_TEST_CHARACTER=2 MELEE_TEST_OPPONENT=8
export MELEE_FLIP_BARRIER_MASK="$mask" MELEE_FLIP_BARRIER_BASE="$base"
export MELEE_CAPTURE_FRAME="$root/$name.ppm"
cp data/state/game.log "$root/previous.log"
: > data/state/game.log
cut -d ' ' -f 1 /proc/uptime > "$root/$name.start"
echo "melee-game-$name-$$" > "$root/$name.marker"
echo "<6>$(cat "$root/$name.marker")" > /dev/kmsg
timeout -s TERM -k 10 180 /bin/sh ./launch.sh &
pid=$!
(
 while kill -0 "$pid" 2>/dev/null; do
  available=$(awk '/MemAvailable:/ {print $2}' /proc/meminfo)
  echo "$(cut -d ' ' -f 1 /proc/uptime) $available" >> "$root/$name.memory"
  echo "$(cut -d ' ' -f 1 /proc/uptime) $(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq) $(cat /sys/class/devfreq/fde60000.gpu/cur_freq) $(cat /sys/class/devfreq/fde60000.gpu/load) dmc=$(cat /sys/class/devfreq/dmc/cur_freq)" >> "$root/$name.sensors"
  if [ "$available" -lt 180000 ]; then
   echo memory-stop > "$root/$name.reason"
   kill -TERM "$pid"; break
  fi
  if test -f "$root/$name.ppm" && grep -q '\[render-detail\]' data/state/game.log; then
   if /tmp/melee-scanout; then cp /tmp/melee-scanout.ppm "$root/$name.scanout.ppm";
   else echo scanout-failed > "$root/$name.scanout-error"; fi
   echo capture-complete > "$root/$name.reason"
   kill -TERM "$pid"; break
  fi
  sleep 2
 done
) &
watch=$!
wait "$pid"
status=$?
echo "$status" > "$root/$name.status"
if [ ! -f "$root/$name.reason" ]; then
 if [ "$status" = 124 ]; then echo watchdog-timeout; else echo process-exit; fi > "$root/$name.reason"
fi
kill "$watch" 2>/dev/null
wait "$watch" 2>/dev/null
cp data/state/game.log "$root/$name.log"
dmesg > "$root/$name.dmesg"
cat /sys/devices/system/cpu/online > "$root/$name.cores"
echo done > "$root/$name.done"
exit 0
