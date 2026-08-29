# Release Checklist

Use this before tagging or uploading a release.

For normal people: build it, test it, make sure the files are the right ones.

For devs: do not mix app/plugin versions, and do not claim audio stability from CI alone.

## Clean Checks

- If you want GitHub to build the release files, run the `Release Artifacts` workflow from the Actions tab.

- Run host tests:

  ```bash
  EQVITA_BUILD_TYPE=Debug bash scripts/test-host-wsl.sh
  ```

- If Equalizer APO or DSP code changed, run the ARMv7 Cortex-A9 fixture test:

  ```bash
  bash scripts/test-armv7-qemu.sh
  ```

- Run release hygiene:

  ```powershell
  .\scripts\check-release-hygiene.ps1
  ```

- Run a Vita Release build:

  ```bash
  EQVITA_ARTIFACT_DIR=build EQVITA_BUILD_TYPE=Release bash scripts/build-wsl.sh
  ```

## Artifact Checks

- Confirm these files exist:
  - `build/app/EQVita.vpk`
  - `build/plugin/eq_speaker.skprx`
- Confirm the VPK does not include test audio files.
- Confirm `icon0.png`, `bg.png`, and `startup.png` are indexed 8-bit PNGs.
- If README media changed, preview `media/app-flow.png`, `media/music-preview.png`, and `media/themes.png` on GitHub or in a Markdown preview.
- If an APNG looks cropped, use [README Media](../app/readme-media.md) and regenerate it with full-frame APNG settings.
- Generate SHA256 sums for the VPK and plugin.

  ```powershell
  .\scripts\create-release-checksums.ps1
  ```

## Version Checks

- Keep `common/eq_shared.h`, `plugin/exports.yml`, `app/CMakeLists.txt`, and [README.md](../../README.md) on the same ABI line.
- Keep the app and plugin from the same build together.
- Do not publish mixed old/new app and plugin binaries.

## Hardware Checks

- Run [hardware-test-checklist.md](../testing/hardware-test-checklist.md) on real Vita hardware.
- Do not claim audio stability from CI alone.
- Save `ur0:data/eqvita/app.log` from the test run.
- If audio work changed, read [audio-stability.md](../audio/audio-stability.md) before tagging.

## Upload

- Upload:
  - `EQVita.vpk`
  - `eq_speaker.skprx`
  - `EQVita.sha256`
- In the release notes, mention the ABI version and any hardware test limits.
- Use [release-notes-template.md](release-notes-template.md) so release notes stay easy to read.
