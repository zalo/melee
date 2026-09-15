#!/bin/sh
# Test build (Aurora PR set, no direct GLES) installed beside the fast build.
# Shares the disc image with the main install; keeps its own config, saves,
# shader cache and logs under /mnt/SDCARD/Ports/melee-native-dev/data.
export MELEE_FLIP_DISC=/mnt/SDCARD/Ports/melee-native/data/disc.img
exec /mnt/SDCARD/Ports/melee-native-dev/launch.sh
