Melee Native 0.1.0 (development build)

Requires an Apple Silicon Mac running macOS 15.5 or newer.
Unzip the app, move it wherever you want, and open Melee Native.
Choose your Super Smash Bros. Melee US 1.02 image (GALE01, revision 2).
ISO, GCM, CISO and RVZ are accepted. The file stays where you chose it;
the game reads its content directly from the image. Keep it accessible while playing.
Canceling the picker exits. A fresh launch asks for an image again.

At the game's initial save prompt, choose No. Saving is not yet working.
Keyboard (click the game window first):
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
