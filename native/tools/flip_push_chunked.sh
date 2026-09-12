#!/bin/bash
# Push a large file to the Flip over a flaky link: 1 MiB chunks, per-chunk
# retries, reassembly on the device, SHA-256 verification.
# Usage: flip_push_chunked.sh <adb> <serial> <local-file> <remote-path>
set -u
adb=$1; serial=$2; src=$3; dst=$4
work=$(mktemp -d)
split -b 1m -d -a 3 "$src" "$work/part."
sum=$(sha256sum "$src" | cut -d' ' -f1)
remote_tmp="$dst.chunks"
run() { timeout 60 "$adb" -s "$serial" "$@"; }
reconnect() { "$adb" disconnect "$serial" >/dev/null 2>&1; sleep 5; "$adb" connect "$serial" >/dev/null 2>&1; sleep 2; }
until run shell "mkdir -p '$remote_tmp'"; do reconnect; done
for part in "$work"/part.*; do
  name=$(basename "$part")
  local_sum=$(sha256sum "$part" | cut -d' ' -f1)
  for attempt in $(seq 1 30); do
    remote_sum=$(run shell "sha256sum '$remote_tmp/$name' 2>/dev/null | cut -d' ' -f1" 2>/dev/null | tr -d '\r')
    if [ "$remote_sum" = "$local_sum" ]; then break; fi
    run push "$part" "$remote_tmp/$name" >/dev/null 2>&1 || reconnect
  done
  echo -n "."
done
echo
for attempt in $(seq 1 10); do
  if run shell "cat '$remote_tmp'/part.* > '$dst.partial' && sha256sum '$dst.partial' | cut -d' ' -f1" | tr -d '\r' | grep -q "^$sum\$"; then
    run shell "mv '$dst.partial' '$dst' && rm -rf '$remote_tmp'" && echo "Verified: $dst" && rm -rf "$work" && exit 0
  fi
  reconnect
done
echo "FAILED: $dst" >&2; rm -rf "$work"; exit 1
