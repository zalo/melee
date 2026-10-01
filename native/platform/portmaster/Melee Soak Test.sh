#!/bin/bash
# Stability test: a CPU-controlled player plays through the game's modes for 30 minutes (rumble off, on
# a copy of the save), then melee/soak-report.txt.gz is written for the tester to share. A build whose
# melee/soak/report-url.txt names a receiver also sends it there. See melee/soak/soak.sh.
# Start+Select stops it at any time.
export MELEE_SOAK=1
exec bash "$(dirname "$0")/Melee.sh"
