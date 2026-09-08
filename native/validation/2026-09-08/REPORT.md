# Native macOS VS test results

900-frame match samples (about 15 seconds including countdown), two CPU players, default costumes. Items are spawned repeatedly. Numeric central-playfield GPU checks; no saved images.

Not exhaustive visual or gameplay acceptance. Does not cover all moves, combinations, costumes, Kirby copy abilities, Pokemon variants, adventure/target stages or saving.

- stages: 7/7 smoke passes.
- combined: 35/35 smoke passes.
- Unique stages exercised in passing matches: 29 (IDs 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 22, 23, 24, 25, 27, 28, 29, 30, 31, 32).
- Unique characters exercised in passing matches: 26 (IDs 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25).
- Unique items exercised in passing matches: 35 (IDs 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34).

Executable SHA-256: `b7f8ccb462572368ef108baa8ca2f935ee045643df486da45099899fd16674dd`, `b8b83383313837e4df64d618b7aa8c2f9e5c85d4902e11436d135640b4e85cbd`, `66a2b428e80e461da95a137fb567a206ac4a26f6baf04b9df4e6856edf75ab57`, `92a57cb858112573db0b5dd0850f2d417d85237fec8d3cdf4e840434df9dd117`, `71ff96b397ebc6ffa1c89b5f80fa93f6ff2041957e027c4c9a09710d321c913a`

| Group | Case | Result | Match FPS range | Log |
| --- | --- | --- | --- | --- |
| stages | Fountain-of-Dreams | smoke-pass | 56.9–59.2 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-220237/game.log>) |
| stages | Pokemon-Stadium | smoke-pass | 56.2–58.5 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-220307/game.log>) |
| stages | Princess-Peach-Castle | smoke-pass | 56.7–59.1 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-220337/game.log>) |
| stages | Kongo-Jungle | smoke-pass | 56.1–59.1 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-220407/game.log>) |
| stages | Brinstar | smoke-pass | 56.9–59.1 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-220437/game.log>) |
| stages | Corneria | smoke-pass | 55.9–59.2 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-220506/game.log>) |
| stages | Yoshis-Story | smoke-pass | 55.5–60.0 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-220536/game.log>) |
| combined | Corneria / Captain-Falcon vs Pikachu / Capsule | smoke-pass | 56.7–60.0 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-220931/game.log>) |
| combined | Yoshis-Story / Donkey-Kong vs Ice-Climbers / Crate | smoke-pass | 57.4–60.0 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-221042/game.log>) |
| combined | Onett / Fox vs Jigglypuff / Barrel | smoke-pass | 56.7–58.0 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-221113/game.log>) |
| combined | Mute-City / Game-and-Watch vs Samus / Egg | smoke-pass | 56.3–60.0 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-221143/game.log>) |
| combined | Rainbow-Cruise / Kirby vs Yoshi / Party-Ball | smoke-pass | 56.5–60.0 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-221619/game.log>) |
| combined | Jungle-Japes / Bowser vs Zelda / Barrel-Cannon | smoke-pass | 56.3–59.3 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-221739/game.log>) |
| combined | Great-Bay / Link vs Sheik / Bob-omb | smoke-pass | 53.4–57.0 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-221811/game.log>) |
| combined | Hyrule-Temple / Luigi vs Falco / Mr-Saturn | smoke-pass | 55.6–59.0 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-221842/game.log>) |
| combined | Brinstar-Depths / Mario vs Young-Link / Heart-Container | smoke-pass | 54.0–59.6 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-221911/game.log>) |
| combined | Yoshis-Island / Marth vs Dr-Mario / Maxim-Tomato | smoke-pass | 46.4–57.1 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-221941/game.log>) |
| combined | Green-Greens / Mewtwo vs Roy / Starman | smoke-pass | 53.7–60.0 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-222011/game.log>) |
| combined | Fourside / Ness vs Pichu / Home-Run-Bat | smoke-pass | 55.3–60.0 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-222041/game.log>) |
| combined | Mushroom-Kingdom / Peach vs Ganondorf / Beam-Sword | smoke-pass | 56.4–60.0 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-222110/game.log>) |
| combined | Mushroom-Kingdom-II / Pikachu vs Captain-Falcon / Parasol | smoke-pass | 56.5–60.0 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-222140/game.log>) |
| combined | Venom / Ice-Climbers vs Donkey-Kong / Green-Shell | smoke-pass | 49.7–59.0 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-222210/game.log>) |
| combined | Poke-Floats / Jigglypuff vs Fox / Red-Shell | smoke-pass | 55.7–60.0 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-222239/game.log>) |
| combined | Big-Blue / Samus vs Game-and-Watch / Ray-Gun | smoke-pass | 55.6–59.8 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-222309/game.log>) |
| combined | Icicle-Mountain / Yoshi vs Kirby / Freezie | smoke-pass | 56.6–59.8 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-222338/game.log>) |
| combined | Flat-Zone / Zelda vs Bowser / Food | smoke-pass | 56.3–59.2 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-222408/game.log>) |
| combined | Dream-Land / Sheik vs Link / Proximity-Mine | smoke-pass | 52.6–59.2 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-222437/game.log>) |
| combined | Yoshis-Island-64 / Falco vs Luigi / Flipper | smoke-pass | 53.5–60.0 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-222507/game.log>) |
| combined | Kongo-Jungle-64 / Young-Link vs Mario / Super-Scope | smoke-pass | 56.1–60.0 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-222537/game.log>) |
| combined | Battlefield / Dr-Mario vs Marth / Star-Rod | smoke-pass | 57.0–60.0 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-222606/game.log>) |
| combined | Final-Destination / Roy vs Mewtwo / Lips-Stick | smoke-pass | 56.3–59.3 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-222636/game.log>) |
| combined | Onett / Pichu vs Ness / Fan | smoke-pass | 56.5–60.0 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-222705/game.log>) |
| combined | Onett / Ganondorf vs Peach / Fire-Flower | smoke-pass | 56.1–59.2 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-222735/game.log>) |
| combined | Onett / Captain-Falcon vs Pikachu / Super-Mushroom | smoke-pass | 53.4–56.7 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-222805/game.log>) |
| combined | Onett / Donkey-Kong vs Ice-Climbers / Poison-Mushroom | smoke-pass | 55.2–59.2 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-222835/game.log>) |
| combined | Onett / Fox vs Jigglypuff / Hammer | smoke-pass | 53.4–58.3 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-222904/game.log>) |
| combined | Onett / Game-and-Watch vs Samus / Warp-Star | smoke-pass | 56.6–60.0 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-222934/game.log>) |
| combined | Onett / Kirby vs Yoshi / Screw-Attack | smoke-pass | 56.8–60.0 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-223004/game.log>) |
| combined | Onett / Bowser vs Zelda / Bunny-Hood | smoke-pass | 56.8–58.6 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-223033/game.log>) |
| combined | Onett / Link vs Sheik / Metal-Box | smoke-pass | 53.6–58.9 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-223103/game.log>) |
| combined | Onett / Luigi vs Falco / Cloaking-Device | smoke-pass | 54.4–57.3 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-223133/game.log>) |
| combined | Onett / Mario vs Young-Link / Poke-Ball | smoke-pass | 56.0–59.9 | [game.log](</Users/joro/prog/c/melee/build/native-runs/20260908-223203/game.log>) |

## Final app and additional checks

The app in `dist/macos/Melee Native.app` is identical to the last packaged
candidate tested in combined cases 5–34. Its executable SHA-256 is
`71ff96b397ebc6ffa1c89b5f80fa93f6ff2041957e027c4c9a09710d321c913a`.
Earlier passing cases were retained across targeted fixes; every case lists its
actual executable and log in the JSON report. Superseded failed attempts remain
in the JSON attempt history.

- 23/23 component tests passed, including sanitizer-backed asset conversion.
- The original GameCube DOL still matches SHA-1
  `08e0bf20134dfcb260699671004527b2d6bb1a45`.
- A normal keyboard replay opened rules, selected one stock, played a match,
  reached Results and returned to character selection. This route uses no matrix
  selection or unlock hooks. The old test cursor movement was corrected after
  it overshot the rules banner.
- Visual spot checks confirmed Fourside's entry scene, Mushroom Kingdom combat
  with fighters/hit effects/Beam Swords, and Icicle Mountain combat/snow.
- The final app signature verifies. The app contains 20 files and four bundled
  libraries, with no disc image or extracted game assets.
- The game targets 60 Hz. Sampled match presentation ranged from 46.4 to 60.0 FPS
  (median 58.3); these Debug-build tests do not establish a locked 60 FPS.

[Normal UI log](/Users/joro/prog/c/melee/build/native-ui-final/game.log)

## Fixes made during the follow-up

- Camera shake used a fake layout spanning separate globals, producing division
  by zero and NaN camera transforms. Native code now reads the actual camera
  descriptor and clears the full native quake-state region.
- Young Link's hookshot wrote outside a local stack variable as a retail
  assembly-matching trick. Native code uses the normal square-root function.
- Pikachu/Pichu Thunder state writes overlapped an item pointer on LP64. They
  now use the actual down-special layout.
- Kirby's Yoshi-copy archive was misclassified as a full fighter. It now uses
  its hat, joint, animation and article schema with sufficient extension slots.
- Test preloading now prepares both selected fighters and verifies both loaded
  identities. Combined cases rotate fighters and items to reduce launches.
- The old non-black framebuffer check was insufficient. Current checks reject
  non-finite cameras and require playfield color/edge variation after countdown.

These checks establish the stated short-match coverage. They do not establish
complete game compatibility, exhaustive item use, every move or copy ability,
or pixel-identical rendering.
