Melee Native 0.1.0 (development build)

Requires an Apple Silicon Mac running macOS 15.5 or newer.
Open the DMG, drag Melee Native to Applications, and open it from Applications.
Drop in your Super Smash Bros. Melee US 1.02 image (GALE01, revision 2),
or click Choose File. Click Play when validation succeeds.
ISO, GCM, CISO and RVZ are accepted. The file stays where you chose it;
the game reads its content directly from the image. Keep it accessible while playing.
The app remembers the image and opens the game automatically on later launches.
If the file becomes unavailable, locate it again in the setup window.
The app menu offers Controls, Change Disc Image and Restart, and Open Logs.
Changing the image ends the current game session.

The app skips the initial save prompt and plays without saving progress.
Keyboard (the game receives focus automatically):
  WASD move; X attack/confirm; Z special/back; C/V jump;
  Q/E shield; R grab; IJKL C-stick; Return start/pause.

This is an incomplete native port. The VS test matrix targets 29 selectable
stages, 26 character entries and 35 common item kinds. Consult the accompanying
validation report for completed cases. These tests do not
certify every move, costume, visual effect, combination or item behavior.
Adventure, target stages and saving remain outside this test matrix. Audio uses a generated
native resampling filter and is not bit-identical to the console DSP.
Launch logs: ~/Library/Logs/Melee Native/game.log

The app is locally ad-hoc signed, not Developer ID signed or Apple-notarized.
It needs no Homebrew installation or Dolphin runtime.

No disc image, extracted game files, or console firmware are included.
The executable still contains recovered game code; removing external assets
does not establish redistribution rights for that code.

This software uses FreeType (https://freetype.org).
Third-party notices are included alongside this file.
