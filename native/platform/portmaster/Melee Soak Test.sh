#!/bin/bash
# Stability test: a CPU-controlled player plays through the game's modes for 30 minutes (rumble off, on
# a copy of the save), then melee/soak-report.txt.gz is written and, when the device is online, sent to
# the port's report collector. A release build first updates itself to the newest release. Both are
# skipped without a network or with a file melee/soak/offline. See melee/soak/soak.sh.
# Start+Select stops it at any time.
export MELEE_SOAK=1
exec bash "$(dirname "$0")/Melee.sh"
