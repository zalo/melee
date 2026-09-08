# Source publication check

Validated on 2026-09-08 after the original checkpoint.

- Fetched full upstream Git history without merging newer upstream changes.
- Built `melee_mac` and component tests in a separate checkout under `/tmp`,
  with freshly bootstrapped dependencies and no disc assets or generated
  GameCube include files in that checkout.
- All 15 asset-free CTest cases passed.
- Fixed packaging notices to use the selected build directory rather than
  implicitly depending on a second `build/native` tree.
- Packaged and verified the relocated ad-hoc signed app: 20 files and four
  bundled libraries, with no disc or extracted asset files in the package.
- Removed the native build's dependency on two generated font includes.
  Both atlases now load from the selected disc's DOL data sections at startup.
  A local byte-for-byte comparison matched both previously extracted atlases.
- The new packaged app passed a 900-frame combined smoke case: Fountain of
  Dreams, Captain Falcon versus Pikachu, Capsule spawning. This is a targeted
  regression check, not a rerun of the complete earlier matrix.
- Original GameCube build remained 100% byte-matching.

The GitHub workflow builds and packages on an Apple Silicon macOS runner and
runs only asset-free tests. It does not launch gameplay or upload game assets.
Hosted CI status must be checked separately from these local results.

The older packaged app identified in CHECKPOINT.md predates the font-loading
change. Public packages should be produced from the publication commit.
