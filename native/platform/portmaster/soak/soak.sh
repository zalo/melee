# Soak test, sourced by Melee.sh when it is started through "Melee Soak Test.sh". That launcher runs
# Melee.sh twice: once with MELEE_SOAK=update (soak_prepare, below), then with MELEE_SOAK=1 for the
# test itself (soak_begin before the game, soak_end after it).
#
# A CPU-controlled player 1 plays through the game's modes for MELEE_SOAK_MINUTES (default 30) with
# rumble off, on a copy of the save so the real one is untouched. Afterwards log.txt and a short
# device summary are packed into melee/soak-report.txt.gz and, when the device is online, posted to
# the address in soak/report-url.txt (native/tools/soak_report_worker receives them). Offline the test
# still completes and the file can be shared by hand.
#
# Before it starts, a release build (one with melee/version.txt) looks at soak/update-url.txt for a
# newer release and installs it, so a tester always soaks the latest build. Without a network that
# check is skipped after a few seconds. An empty file melee/soak/offline (or MELEE_SOAK_OFFLINE=1)
# turns both off; the normal launcher never contacts anything.

SOAK_DIR="$GAMEDIR/soak"
SOAK_REPORT="$GAMEDIR/soak-report.txt"
SOAK_MINUTES="${MELEE_SOAK_MINUTES:-30}"
# A swap-less handheld that runs out of memory hangs instead of failing; stop the game before that.
SOAK_MIN_AVAIL_KB="${MELEE_SOAK_MIN_AVAIL_KB:-40000}"

# Prints the first line of the address file $1 when the soak test may use the network.
soak_address() {
  [ -z "${MELEE_SOAK_OFFLINE:-}" ] && [ ! -e "$SOAK_DIR/offline" ] || return
  head -n 1 "$SOAK_DIR/$1" 2>/dev/null | tr -d '\r\n '
}

soak_avail_kb() { sed -n 's/^MemAvailable:[^0-9]*\([0-9]*\).*/\1/p' /proc/meminfo; }
# $1 = pid. /proc/<pid>/status puts a tab between the name and the number.
soak_rss_kb() { sed -n 's/^VmRSS:[^0-9]*\([0-9]*\).*/\1/p' "/proc/$1/status" 2>/dev/null; }

# $1 = address, $2 = file, $3 = time limit in seconds
soak_fetch() {
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL --connect-timeout 8 -m "$3" -o "$2" "$1" 2>/dev/null
  elif command -v wget >/dev/null 2>&1; then
    wget -q -T 15 -O "$2" "$1" 2>/dev/null
  else
    return 1
  fi
}

# Copies a release tree ($1: Melee.sh, "Melee Soak Test.sh", melee/...) over the installed port. With
# $2, the files about to be replaced are first saved there in the same layout. Each file is written
# beside its target and renamed into place: bash reads Melee.sh as it runs it, so the running
# launcher must never be rewritten in place.
soak_install_tree() {
  ports=$(dirname "$GAMEDIR")
  (cd "$1" && find . -type f ! -path ./port.json) | while IFS= read -r file; do
    file=${file#./}
    if [ -n "$2" ] && [ -f "$ports/$file" ]; then
      mkdir -p "$2/$(dirname "$file")" && cp -f "$ports/$file" "$2/$file" || exit 1
    fi
    mkdir -p "$ports/$(dirname "$file")" && cp -f "$1/$file" "$ports/$file.new" || exit 1
    [ -x "$ports/$file" ] && chmod +x "$ports/$file.new"
    mv -f "$ports/$file.new" "$ports/$file" || exit 1
  done
}

# Installs the latest release when it is newer than this one. Every failure (no network, no tools,
# bad download, full card) leaves the installed build as it is.
soak_update() {
  base=$(soak_address update-url.txt)
  case "$base" in https://*) ;; *) return ;; esac
  # Only release builds carry a version; a development build is never replaced.
  installed=$(head -n 1 "$GAMEDIR/version.txt" 2>/dev/null | tr -d '\r\n ')
  case "$installed" in portmaster-[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]-*) ;; *) return ;; esac
  [ -f "$(dirname "$GAMEDIR")/Melee.sh" ] || return
  command -v sha256sum >/dev/null 2>&1 && command -v unzip >/dev/null 2>&1 || return
  new="$GAMEDIR/update/new"
  rm -rf "$new"
  mkdir -p "$new" || return
  soak_fetch "$base/melee-version.txt" "$new/version" 20 || return
  read -r tag sum rest < "$new/version"
  case "$tag" in portmaster-[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]-*) ;; *) return ;; esac
  # Tags are portmaster-<yyyymmdd>-<commit>: never go back to an older day, nor to a release that
  # failed to start here before.
  [ "$tag" != "$installed" ] && [ "$(echo "$tag" | cut -c12-19)" -ge "$(echo "$installed" | cut -c12-19)" ] || return
  [ "$tag" != "$(cat "$GAMEDIR/update/rejected" 2>/dev/null)" ] || return
  # Room for the download, its unpacked copy and the saved previous build.
  [ "$(df -Pk "$GAMEDIR" | awk 'NR==2 {print $4}')" -ge 150000 ] || return

  pm_message "Soak test: downloading the newer release $tag ..."
  soak_fetch "$base/melee.zip" "$new/melee.zip" 900 || return
  [ "$(sha256sum "$new/melee.zip" | cut -d' ' -f1)" = "$sum" ] || return
  unzip -q -o "$new/melee.zip" -d "$new/tree" || return
  [ -s "$new/tree/Melee.sh" ] && [ -s "$new/tree/melee/melee.aarch64" ] \
    && [ "$(head -n 1 "$new/tree/melee/version.txt" 2>/dev/null)" = "$tag" ] || return
  rm -rf "$GAMEDIR/update/previous"
  if ! soak_install_tree "$new/tree" "$GAMEDIR/update/previous"; then
    soak_install_tree "$GAMEDIR/update/previous"
    return
  fi
  rm -rf "$new"
  # soak_end puts the previous build back if this one cannot even start.
  echo "$tag" > "$GAMEDIR/update/fresh"
  pm_message "Soak test: updated $installed -> $tag."
  sleep 3
}

# After an update, a game that dies before its first match is the update's fault: restore the build
# that was here and remember not to fetch that release again. $1 = result line; prints what to add.
soak_update_verdict() {
  [ -f "$GAMEDIR/update/fresh" ] || return
  tag=$(cat "$GAMEDIR/update/fresh")
  rm -f "$GAMEDIR/update/fresh"
  case "$1" in CRASHED*) ;; *) return ;; esac
  grep -q '^\[puppet\] match ' "$GAMEDIR/log.txt" 2>/dev/null && return
  if soak_install_tree "$GAMEDIR/update/previous"; then
    echo "$tag" > "$GAMEDIR/update/rejected"
    echo "; update $tag did not start, previous build restored"
  fi
}

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
    echo "version: $(head -n 1 "$GAMEDIR/version.txt" 2>/dev/null || echo development build)"
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
  url=$(soak_address report-url.txt)
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
    pm_message "Soak test: $1. Report sent (id ${answer#ok }); a copy is in melee/soak-report.txt.gz."
  else
    pm_message "Soak test: $1. Report saved as melee/soak-report.txt.gz - please share it."
  fi
  sleep 12
}

# A marker left behind means the last soak never reached its end (freeze, power loss, system kill):
# report it from that run's log, $1.
soak_report_unfinished() {
  if [ -f "$SOAK_DIR/running" ] && grep -q '^\[puppet\] ' "$1" 2>/dev/null; then
    MELEE_TEST_MODES=$(cat "$SOAK_DIR/running") soak_write_report "did not finish (freeze, power loss or killed by the system)" "$1"
    soak_send_report >/dev/null
    cp -f "$SOAK_REPORT.gz" "$GAMEDIR/soak-report-unfinished.txt.gz" 2>/dev/null
  fi
  rm -f "$SOAK_DIR/running"
}

# The MELEE_SOAK=update pass, called before Melee.sh rotates its log. It is a process of its own so
# that the test starts on the launcher and files installed here, from the frontend's environment
# rather than one PortMaster's control.txt has already been through.
soak_prepare() {
  mkdir -p "$SOAK_DIR"
  soak_report_unfinished "$GAMEDIR/log.txt"
  soak_update
}

soak_begin() {
  if [ ! -d "$GAMEDIR/runtime/config/melee-native" ]; then
    pm_message "Soak test: start the game normally once first so it can create its save."
    sleep 10
    exit 1
  fi
  mkdir -p "$SOAK_DIR"
  soak_report_unfinished "$GAMEDIR/log.prev.txt"
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
  # Start+Select reaches the game twice: gptokeyb2 kills it, and the game sees the buttons and leaves.
  elif [ "$1" -eq 137 ] || [ "$1" -eq 143 ] || grep -q '^\[exit\] quit requested' "$GAMEDIR/log.txt" 2>/dev/null; then
    result="stopped by the exit hotkey"
  elif [ "$1" -eq 0 ]; then
    result="the game exited by itself"
  else
    result="CRASHED (exit status $1)"
  fi
  rm -f "$SOAK_DIR/running"
  result="$result$(soak_update_verdict "$result")"
  soak_write_report "$result" "$GAMEDIR/log.txt"
  soak_finish_message "$result"
}
