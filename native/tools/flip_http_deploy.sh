#!/bin/bash
# Deploy one large file to the Flip by letting the device pull it over HTTPS.
#
# adb push over the cloudflared TCP tunnel is chatty and stalls to "device
# offline" as soon as the Flip's Wi-Fi link saturates; a 20 MB push took hours
# in 1 MiB chunks. A single HTTPS download by the device (curl on the Flip,
# python http.server here behind a cloudflared quick tunnel) moved the same
# binary in about five minutes. The download runs as a holder job
# (flip_holder.sh), so an adb drop does not interrupt it.
#
# Usage: flip_http_deploy.sh <adb> <serial> <local-file> <remote-path> [port]
# Requires: python3, cloudflared, gzip on this host; curl and gzip on the Flip.
set -u
adb=$1; serial=$2; src=$3; dst=$4; port=${5:-18471}
here=$(cd "$(dirname "$0")" && pwd)
holder="$here/flip_holder.sh"
[ -f "$src" ] || { echo "no such file: $src" >&2; exit 1; }
sum=$(sha256sum "$src" | cut -d' ' -f1)
serve=$(mktemp -d); gzip -6 -c "$src" > "$serve/payload.gz"
cleanup() { [ -n "${http_pid:-}" ] && kill "$http_pid" 2>/dev/null; [ -n "${cf_pid:-}" ] && kill "$cf_pid" 2>/dev/null; rm -rf "$serve"; }
trap cleanup EXIT
( cd "$serve" && exec python3 -m http.server "$port" --bind 127.0.0.1 ) > "$serve/http.log" 2>&1 & http_pid=$!
cloudflared tunnel --url "http://127.0.0.1:$port" --no-autoupdate > "$serve/cf.log" 2>&1 & cf_pid=$!
url=""; for i in $(seq 1 30); do url=$(grep -o 'https://[a-z0-9-]*\.trycloudflare\.com' "$serve/cf.log" | head -1); [ -n "$url" ] && break; sleep 2; done
[ -n "$url" ] || { echo "quick tunnel did not come up:"; tail -5 "$serve/cf.log"; exit 1; }
echo "serving $(stat -c %s "$serve/payload.gz") bytes at $url/payload.gz"
"$holder" "$adb" "$serial" status 2>/dev/null | grep -Eq 'started|job-done' || "$holder" "$adb" "$serial" start || exit 1
job=$(mktemp); cat > "$job" <<SH
dst='$dst'; want=$sum; tmp=/tmp/flip_http_deploy.gz
rm -f \$tmp
for t in 1 2 3 4 5 6 7 8; do
  curl -sS -L --retry 8 --retry-delay 3 -C - -o \$tmp '$url/payload.gz' && break
  sleep 5
done
gunzip -c \$tmp > "\$dst.partial" || { echo gunzip-failed; exit 1; }
got=\$(sha256sum "\$dst.partial" | cut -d' ' -f1); echo got=\$got
if [ "\$got" = "\$want" ]; then mv "\$dst.partial" "\$dst"; chmod +x "\$dst"; rm -f \$tmp; echo INSTALLED; else rm -f "\$dst.partial"; echo BAD-SHA; exit 1; fi
SH
"$holder" "$adb" "$serial" job "$job" || exit 1; rm -f "$job"
"$holder" "$adb" "$serial" wait 1800 || exit 1
"$holder" "$adb" "$serial" log | tail -3 | grep -q INSTALLED && { echo "Installed: $dst ($sum)"; exit 0; }
"$holder" "$adb" "$serial" log | tail -5; exit 1
