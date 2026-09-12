# Flip memory-card saves (2026-09-11 / 2026-09-12)

Device: Miyoo Flip V2, remote over the cloudflared tunnel. Logs are not tracked
(`*.log` is ignored); they stay on the device under
`data/diagnostics/save-trials/` and were read as summarised here.

## v146: first save created, crash at the title screen (user report)

- `user-first-save.gci` is the save the game wrote (11 blocks, 90176 bytes,
  header valid per `check_gci.py`).
- The crash tail (`user-crash-game.tail.log`, last 400 lines of the device
  log) ends in `[native-crash]`; the LD_PRELOAD tracer showed `memcmp` in the
  card verify step reading through a 32-bit address:
```

```
  Cause: `int temp_r24` in `lbcardgame.c` truncating the banner pointer.

## v149: both device trials pass

| trial | input script | result |
| --- | --- | --- |
| create-v149 | `native/tests/create-save.input` on an empty card | new `.gci` at 14 s, scenes 28 → 0 → 1, ran to the 150 s timeout (status 143 = SIGTERM), no `[native-crash]` |
| load-v149 | `native/tests/load-save.input` with the save present | save recognised (no create prompt), scenes 28 → 0 → 1, ran to the 110 s timeout, no `[native-crash]` |

- `v149-created-save.gci` is the save from create-v149: 11 blocks, first block
  5, banner format 2, comment "Super Smash Bros. Melee / Game Data 2026/09/12".
- Both trials ran under `LD_PRELOAD=flip-crash-trace.so` via
  `flip_save_trial.sh`, queued through `flip_holder.sh`.
