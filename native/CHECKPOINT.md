# Native macOS checkpoint, 2026-09-08

Local source checkpoint on `native-macos`; nothing published to GitHub.
Upstream remains `https://github.com/doldecomp/melee.git`. The checkout is
shallow. The next publishing step is a GitHub fork retaining original history,
with this branch as the fork's default and the original repository as upstream.

## Current state

- Native Apple Silicon app, macOS 15.5+, Metal rendering, keyboard input.
- User selects a supported US 1.02 disc image for assets; no Dolphin required.
- Packaged app and ZIP remain locally in `dist/macos/`, excluded from Git.
- Final executable SHA-256:
  `71ff96b397ebc6ffa1c89b5f80fa93f6ff2041957e027c4c9a09710d321c913a`.
- Ad-hoc signing only; Developer ID signing and notarization are outstanding.

## Recorded validation

The preserved reports in `validation/2026-09-08/` record the previous test run;
no new gameplay tests were run to create this checkpoint. Their local log paths
refer to this development machine, and raw logs remain under ignored `build/`.

- 29 selectable VS stages, 26 character entries, 35 common item kinds covered
  by short-match smoke tests, including countdown.
- 23/23 component tests passed; original GameCube DOL remained byte-matching.
- Normal keyboard menu, one-stock match, Results and return flow passed.
- Earlier passing cases span targeted builds, as recorded per case.
- These checks do not certify all moves, effects, combinations or modes.
- Saving is not working. Adventure and target stages remain outside coverage.
- A locked 60 FPS is not verified.

## Resume

Before publication: fetch upstream history, prepare the fork's main README,
verify a fresh source-only checkout builds, preserve notices, and audit the
exact files to publish. Upload the app through Releases rather than source Git.
Keep game assets, disc images, build products and logs out of the repository.
Do not treat this checkpoint as a completed public-release preparation.
