# Onboarding verification

Date: September 9, 2026. Local Apple Silicon Mac; macOS app build.

## Delivered

- Native setup window with SSBM branding, file picker, drag-and-drop target,
  asynchronous disc validation, controls preview and Play button.
- Remembered disc URL bookmark, automatic subsequent launch and missing-file recovery.
- Native app menu for controls, image-change restart and logs.
- Graphical launches initialize a session without saving and skip the boot save prompt.
- SSBM app icon, DMG with Applications shortcut, and ZIP packaging.
- Optional Developer ID signing and notarization through packaging environment settings.
- CI uploads the installable development artifacts.

## Evidence

- `cmake --build build/native-app --target melee_mac all --parallel 4`: passed.
- `ctest --test-dir build/native-app -L melee --output-on-failure`: 15/15 passed.
- `ruby -c native/tools/package_app.rb`: passed.
- `ruby native/tools/package_app.rb build/native-app dist/onboarding-20260909-v2`: passed;
  DMG approximately 10 MB, ZIP approximately 9.4 MB, 23 app files and four bundled libraries.
- `codesign --verify --deep --strict`: passed for the packaged app and the app mounted from DMG.
- `hdiutil verify`: valid DMG checksum. Mounted read-only, inspected contents and verified
  the Applications symlink targets `/Applications`, then unmounted.
- `CFBundleIconFile` resolves to the bundled `MeleeNative.icns`.
- UI: invalid image reports an error and leaves Play disabled.
- UI: supported CISO validates, enables Play and launches the packaged game.
- UI: graphical boot skips the save prompt and reaches the opening movie.
- UI: Return, S and X navigate through title, main menu and VS mode to character selection.
- UI: Controls menu opens and returns to the game.
- UI: quit and relaunch opens directly into the game with no setup input.
- UI: Change Disc Image and Restart returns to setup.
- UI: a temporary missing-file bookmark displays Locate File with Play disabled.
  Restored the previously validated image preference after testing.
- `python3 /Users/joro/.codex/skills/autoreview/scripts/autoreview --mode local`:
  final structured review clean, no findings. Review inspected the final code;
  builds and UI testing were performed separately as described above.
- `git diff --check`: passed.

## Scope and limitations

This is a locally signed development package. No Developer ID Application certificate
was available, so notarization was not executed or verified. This machine is also the
development machine; a clean, quarantined-download Gatekeeper acceptance run remains
part of the public release process. The drag target was implemented but not manually
exercised in this pass. This verification reaches character selection, not a complete
match; existing gameplay validation reports cover the earlier VS matrix.

Build logs, package manifest and structured review outputs are under the ignored
`build/native-onboarding-*` and `dist/onboarding-20260909-v2` paths.
