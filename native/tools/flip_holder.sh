#!/bin/bash
# Keep the Flip awake and run device-side jobs with the display handed over.
#
# MainUI owns the idle-sleep timer (about 10 minutes) and the framebuffer. When
# MainUI receives SIGTERM the launcher runs /tmp/cmd_to_run.sh and restarts
# MainUI once that script exits. This tool installs a "holder" there that loops
# until released and runs /tmp/hold-job.sh whenever one appears, so:
#   - the device cannot auto-sleep (and kill its cloudflared tunnel) while held,
#   - jobs run detached from adb and survive tunnel drops,
#   - games launched from a job get the display like a MainUI launch would.
# State on the device: /tmp/hold.state (started|running|job-done rc=N|released),
# /tmp/hold-job.log (output of the last job).
#
# Usage: flip_holder.sh <adb> <serial> start
#        flip_holder.sh <adb> <serial> job <local-sh-script>   # queue a job
#        flip_holder.sh <adb> <serial> status | log
#        flip_holder.sh <adb> <serial> wait [seconds]           # until job-done
#        flip_holder.sh <adb> <serial> release                  # MainUI returns
set -u
adb=$1; serial=$2; cmd=$3; shift 3
reconnect() { "$adb" disconnect "$serial" >/dev/null 2>&1; sleep 3; "$adb" connect "$serial" >/dev/null 2>&1; sleep 2; }
run() { # retry an adb command across tunnel drops
  local t; for t in 1 2 3 4 5 6; do timeout 90 "$adb" -s "$serial" "$@" 2>/dev/null && return 0; reconnect; done; return 1; }
sh_() { run shell "$@" | tr -d '\r'; }
case "$cmd" in
start)
  if [ -n "$(sh_ 'pidof melee_native')" ]; then echo "game is running; not touching MainUI" >&2; exit 1; fi
  if [ -z "$(sh_ 'pidof MainUI')" ] && sh_ 'cat /tmp/hold.state' | grep -Eq 'started|running|job-done'; then echo "holder already active"; exit 0; fi
  run shell 'cat > /tmp/cmd_to_run.sh <<"SH"
#!/bin/sh
echo started > /tmp/hold.state
i=0
while [ ! -f /tmp/hold-release ] && [ $i -lt 43200 ]; do
  if [ -f /tmp/hold-job.sh ]; then
    mv /tmp/hold-job.sh /tmp/hold-job.running; echo running > /tmp/hold.state
    sh /tmp/hold-job.running > /tmp/hold-job.log 2>&1; echo "job-done rc=$?" > /tmp/hold.state
    rm -f /tmp/hold-job.running
  fi
  sleep 1; i=$((i+1))
done
rm -f /tmp/hold-release; echo released > /tmp/hold.state
SH
chmod +x /tmp/cmd_to_run.sh; rm -f /tmp/hold-release /tmp/hold-job.sh /tmp/hold.state; kill -TERM $(pidof MainUI)' || exit 1
  sleep 4; echo "holder: $(sh_ 'cat /tmp/hold.state')" ;;
job)
  [ -f "$1" ] || { echo "no such script: $1" >&2; exit 1; }
  sh_ 'cat /tmp/hold.state' | grep -Eq 'started|job-done' || { echo "holder not idle: $(sh_ 'cat /tmp/hold.state')" >&2; exit 1; }
  run push "$1" /tmp/hold-job.sh.new >/dev/null && run shell 'mv /tmp/hold-job.sh.new /tmp/hold-job.sh' && echo queued ;;
status) sh_ 'cat /tmp/hold.state' ;;
log) sh_ 'cat /tmp/hold-job.log' ;;
wait)
  limit=${1:-600}; t=0
  while [ "$t" -lt "$limit" ]; do
    s=$(sh_ 'cat /tmp/hold.state'); case "$s" in job-done*) echo "$s"; exit 0;; esac
    sleep 10; t=$((t + 10))
  done; echo "timeout (state: $(sh_ 'cat /tmp/hold.state'))" >&2; exit 1 ;;
release) run shell 'touch /tmp/hold-release'; sleep 3; echo "holder: $(sh_ 'cat /tmp/hold.state') MainUI=$(sh_ 'pidof MainUI')" ;;
*) echo "unknown command: $cmd" >&2; exit 2 ;;
esac
