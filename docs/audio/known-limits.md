# Known Limits

EQVita is a Vita kernel plugin plus a normal Vita app. That means some things can only be proven on real hardware.

For normal people: if it sounds good on your Vita, that is the test that matters most.

For people who want the details: host tests catch shared logic bugs, but they cannot prove kernel hook timing, port behavior, suspend/resume, Bluetooth, wired output, or heavy-game stability.

## Audio Stability

- Short bypasses can happen when the Vita opens, closes, or reconfigures audio ports.
- Heavy games can stress audio timing more than the home screen.
- A clipping counter does not explain every ugly sound. Crackle, static, popping, and stutter can also come from timing or port changes.
- EQVita logs useful status when the app opens, so open EQVita once after an audio issue before sharing `ur0:data/eqvita/app.log`.
- Imported Equalizer APO curves can contain up to 48 ordered operations. Curves near that limit need real-hardware stress testing because host tests cannot prove Vita audio-hook timing.

## Equalizer APO Import

- Supported commands, paths, and numeric limits are documented in [Using Equalizer APO Configs](equalizer-apo.md).
- Windows `Device` selectors are ignored; every active non-device line is treated as part of the Vita curve.
- Unsupported active commands fail the import instead of being silently approximated.
- PEQ configs are selected from `ur0:data/eqvita/peq/`. The app creates that directory automatically.
- Speaker, wired, and Bluetooth assignments are stored in `ur0:data/eqvita/output-peq.eqpf`; unassigned routes bypass while output-profile mode is active.
- Imported PEQ is exclusive: graphic bands and Bass guard are disabled, and APO Exact preserves the config's requested preamp. APO Exact hard-saturates out-of-range PCM; normal modes use the soft limiter.
- L/R `Copy` matrices and channel-specific filters are stereo features. Mono Vita streams retain the left-channel path.
- Frequencies at or above the current stream's Nyquist frequency are constrained to a stable value below Nyquist.

## Output Routes

- Vita speakers, wired headphones, and Bluetooth can behave differently.
- Speaker-only mode is meant to affect only the Vita speakers.
- All-output mode also allows wired headphones and Bluetooth.
- Output PEQ profiles provide three independent curves. Wired state is checked in the kernel. The app uses the AVConfig route query already shipped by official EQVita v1.14 to distinguish Bluetooth from speakers while the app is running, then persists the last validated route hint for boot-time use.
- No Bluetooth kernel library is imported. If Bluetooth is connected or disconnected while EQVita is closed, the saved hint cannot update until the app is opened again; wired insertion still overrides a saved speaker/Bluetooth hint directly in the plugin.
- Hardware testing should mention which output was used.

## Version Matching

Keep `EQVita.vpk` and `eq_speaker.skprx` from the same release.

Mixed versions can fail, bypass, or report an ABI mismatch.

## Emulators

Vita emulators are useful for some app-side checks, but they are not a replacement for a real Vita when testing kernel audio behavior.

## Release Honesty

Do not claim a release is audio-stable from CI alone.

Before publishing, test on real hardware and record what was tested in the release notes.
