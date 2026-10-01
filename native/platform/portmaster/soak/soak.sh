# Soak test, sourced by Melee.sh when it is started through "Melee Soak Test.sh".
#
# A CPU-controlled player 1 plays through the game's modes for MELEE_SOAK_MINUTES (default 30) with
# rumble off, on a copy of the save so the real one is untouched. Afterwards log.txt and a short
# device summary are packed into melee/soak-report.txt.gz. When soak/report-url.txt holds an address
# and the device is online, the report is also sent there; offline the test still completes and the
# file can be shared by hand. The normal launcher never sends anything.

SOAK_DIR="$GAMEDIR/soak"
SOAK_REPORT="$GAMEDIR/soak-report.txt"
SOAK_MINUTES="${MELEE_SOAK_MINUTES:-30}"
# A swap-less handheld that runs out of memory hangs instead of failing; stop the game before that.
SOAK_MIN_AVAIL_KB="${MELEE_SOAK_MIN_AVAIL_KB:-40000}"

soak_avail_kb() { sed -n 's/^MemAvailable:[^0-9]*\([0-9]*\).*/\1/p' /proc/meminfo; }
# $1 = pid. /proc/<pid>/status puts a tab between the name and the number.
soak_rss_kb() { sed -n 's/^VmRSS:[^0-9]*\([0-9]*\).*/\1/p' "/proc/$1/status" 2>/dev/null; }

soak_stop_game() {
  pid=$(pidof melee.aarch64) || return
  # A polite stop first: the game clears its crash-loop marker on SIGTERM and exits.
  kill -TERM $pid 2>/dev/null
  sleep 3
  pid=$(pidof melee.aarch64) && kill -9 $pid 2>/dev/null
}

# Runs beside the game: samples memory once a minute and ends the test at the time limit.
soak_watchdog() {
  t=0
  limit=$((SOAK_MINUTES * 60))
  while [ $t -lt $limit ]; do
    sleep 10
    t=$((t + 10))
    pid=$(pidof melee.aarch64) || continue
    avail=$(soak_avail_kb)
    if [ $((t % 60)) -eq 0 ]; then
      echo "t=${t}s rss=$(soak_rss_kb $pid)kB avail=${avail}kB" >> "$SOAK_DIR/mem.txt"
    fi
    if [ -n "$avail" ] && [ "$avail" -lt "$SOAK_MIN_AVAIL_KB" ]; then
      echo "stopped after ${t}s: low memory (${avail} kB available)" > "$SOAK_DIR/result"
      soak_stop_game
      return
    fi
  done
  echo "completed ${SOAK_MINUTES} minutes" > "$SOAK_DIR/result"
  soak_stop_game
}

# $1 = result line, $2 = log file
soak_write_report() {
  {
    echo "melee soak report v1"
    echo "result: $1"
    echo "date: $(date -u '+%Y-%m-%d %H:%M:%S') UTC"
    echo "binary: $(md5sum "$GAMEDIR/melee.aarch64" 2>/dev/null | cut -c1-12)"
    echo "modes: $MELEE_TEST_MODES"
    echo "device: ${DEVICE_NAME:-unknown} cpu=${DEVICE_CPU:-unknown} ram=${DEVICE_RAM:-?}GB display=${DISPLAY_WIDTH:-?}x${DISPLAY_HEIGHT:-?}"
    echo "cfw: ${CFW_NAME:-unknown} ${CFW_VERSION:-}"
    echo "kernel: $(uname -r) $(uname -m)"
    echo "memtotal: $(sed -n 's/^MemTotal: *//p' /proc/meminfo)"
    echo "matches: $(grep -c '^\[puppet\] match ' "$2" 2>/dev/null)"
    echo "last: $(grep '^\[puppet\] ' "$2" 2>/dev/null | tail -n 1)"
    echo "== memory =="
    cat "$SOAK_DIR/mem.txt" 2>/dev/null
    echo "== log =="
    # The end of a log is where a crash or freeze shows; keep the start too for the device details.
    if [ "$(wc -c < "$2")" -gt 6000000 ]; then
      head -c 1000000 "$2"; echo; echo "== log cut =="; tail -c 5000000 "$2"
    else
      cat "$2"
    fi
  } > "$SOAK_REPORT" 2>/dev/null
  gzip -f "$SOAK_REPORT"
}

# Prints the receiver's answer ("ok <id>") on success. Never blocks for long and never fails the launch.
soak_send_report() {
  url=$(head -n 1 "$SOAK_DIR/report-url.txt" 2>/dev/null | tr -d '\r\n ')
  case "$url" in https://*) ;; *) return 1 ;; esac
  if command -v curl >/dev/null 2>&1; then
    curl -fsS --connect-timeout 10 -m 120 -H "Content-Type: application/gzip" \
      --data-binary "@$SOAK_REPORT.gz" "$url" 2>/dev/null
  elif command -v wget >/dev/null 2>&1; then
    wget -q -T 30 -O - --header "Content-Type: application/gzip" --post-file "$SOAK_REPORT.gz" "$url" 2>/dev/null
  else
    return 1
  fi
}

soak_finish_message() {
  if answer=$(soak_send_report) && [ "${answer#ok }" != "$answer" ]; then
    pm_message "Soak test: $1. Report sent (id ${answer#ok })."
  else
    pm_message "Soak test: $1. Report saved as melee/soak-report.txt.gz - please share it."
  fi
  sleep 12
}

soak_begin() {
  if [ ! -d "$GAMEDIR/runtime/config/melee-native" ]; then
    pm_message "Soak test: start the game normally once first so it can create its save."
    sleep 10
    exit 1
  fi
  mkdir -p "$SOAK_DIR"
  # A marker left behind means the last soak never reached its end (freeze, power loss, system kill).
  if [ -f "$SOAK_DIR/running" ] && grep -q '^\[puppet\] ' "$GAMEDIR/log.prev.txt" 2>/dev/null; then
    MELEE_TEST_MODES=$(cat "$SOAK_DIR/running") soak_write_report "did not finish (freeze, power loss or killed by the system)" "$GAMEDIR/log.prev.txt"
    soak_send_report >/dev/null
    cp -f "$SOAK_REPORT.gz" "$GAMEDIR/soak-report-unfinished.txt.gz" 2>/dev/null
  fi
  rm -f "$SOAK_DIR/result" "$SOAK_DIR/mem.txt"

  # The puppet plays on a copy of the save: records, unlocks and settings of the real one stay as they are.
  rm -rf "$GAMEDIR/runtime/soak-config"
  cp -r "$GAMEDIR/runtime/config" "$GAMEDIR/runtime/soak-config"
  export XDG_CONFIG_HOME="$GAMEDIR/runtime/soak-config"

  export MELEE_INPUT_SCRIPT="$SOAK_DIR/puppet_menu1.txt"
  export MELEE_TEST_P1_CPU=9 MELEE_TEST_NO_RUMBLE=1 MELEE_TRACE_INPUT=1 AURORA_MEM_LOG=1
  export MELEE_TEST_MODES="${MELEE_TEST_MODES:-classic,adventure,allstar,event:5,targets:3,homerun,10man,3min,training,vs,stamina,giant,tiny,lightning,slomo}"
  echo "$MELEE_TEST_MODES" > "$SOAK_DIR/running"

  pm_message "Soak test: the game plays itself for $SOAK_MINUTES minutes, then makes a report. Start+Select stops it."
  sleep 5
  soak_watchdog &
  soak_watchdog_pid=$!
}

# $1 = exit status of the game
soak_end() {
  kill $soak_watchdog_pid 2>/dev/null
  if [ -f "$SOAK_DIR/result" ]; then
    result=$(cat "$SOAK_DIR/result")
  elif [ "$1" -eq 137 ] || [ "$1" -eq 143 ]; then
    result="stopped by the exit hotkey"
  elif [ "$1" -eq 0 ]; then
    result="the game exited by itself"
  else
    result="CRASHED (exit status $1)"
  fi
  rm -f "$SOAK_DIR/running"
  soak_write_report "$result" "$GAMEDIR/log.txt"
  soak_finish_message "$result"
}
