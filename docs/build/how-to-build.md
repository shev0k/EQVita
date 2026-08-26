# Build EQVita

This is the simple build guide.

For normal people: install VitaSDK, run the WSL build script, then copy the VPK and plugin to the Vita.

For devs: the helper scripts build inside the WSL filesystem cache, then copy the final artifacts back into this repo. That avoids timestamp weirdness from building directly on `/mnt/c`.

## Requirements

- VitaSDK
- WSL2 on Windows, preferably Ubuntu
- CMake 3.16 or newer
- `make` or `ninja`

Windows builds should use WSL2. The expected VitaSDK environment is:

```bash
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"
```

If VitaSDK is missing, follow the installer flow from https://vitasdk.org/.

From WSL as root or with sudo:

```bash
apt-get update
apt-get install -y make git cmake python3 wget ca-certificates curl xz-utils
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"
git clone https://github.com/vitasdk/vdpm /tmp/vdpm
cd /tmp/vdpm
./bootstrap-vitasdk.sh
./install-all.sh
```

## Build Artifacts
From Windows PowerShell:

```powershell
.\scripts\build-wsl.ps1
```

From WSL:

```bash
EQVITA_ARTIFACT_DIR=build EQVITA_BUILD_TYPE=Release bash scripts/build-wsl.sh
```
Outputs:
- `build/plugin/eq_speaker.skprx`
- `build/app/EQVita.vpk`

## Diagnostic Build

Normal Release builds keep the audio hook lighter. For one-off debugging, build with `EQVITA_AUDIO_DIAGNOSTICS` enabled:

```bash
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"
cmake -S . -B /tmp/eqvita-diag-build -DCMAKE_BUILD_TYPE=Release -DEQVITA_AUDIO_DIAGNOSTICS=ON
cmake --build /tmp/eqvita-diag-build
mkdir -p build-diagnostics/app build-diagnostics/plugin
cp /tmp/eqvita-diag-build/app/EQVita.vpk build-diagnostics/app/EQVita.vpk
cp /tmp/eqvita-diag-build/plugin/eq_speaker.skprx build-diagnostics/plugin/eq_speaker.skprx
```

Use the diagnostic build only when collecting deeper timing, peak, bypass, or config-mismatch logs.

## Host Tests

Host tests do not require VitaSDK.

From WSL/Linux, prefer the host-test wrapper. It builds in the WSL filesystem cache so Make does not trip over `/mnt/c` timestamp drift:

```bash
EQVITA_BUILD_TYPE=Debug bash scripts/test-host-wsl.sh
```

If you are already on a normal Linux filesystem, the direct CMake flow is also fine:

```bash
cmake -S . -B build-host -DEQVITA_HOST_TESTS=ON
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

These cover shared ABI/preset validation, DSP behavior, sparse audio-port tracking, app persistence, and UI source checks. They do not replace hardware testing.

For the Equalizer APO parser and DSP math, an additional test cross-compiles the real-config fixtures as ARMv7 hard-float code and executes them with QEMU's Cortex-A9 CPU model. On Ubuntu/WSL, install `gcc-arm-linux-gnueabihf` and `qemu-user`, then run:

```bash
bash scripts/test-armv7-qemu.sh
```

## App Media Preview Support

The app links VitaSDK audio/file APIs plus decoder libraries for Music Preview:

- `SceAudio_stub`
- `SceAudiodec_stub`
- `SceAppMgr_stub`
- `SceIofilemgr_stub`
- `vorbisfile`, `vorbis`, `ogg`
- `mpg123`

The VPK should not include any test songs. Users choose their own `OGG`, `MP3`, or `WAV` files from Vita storage.

## Release Checks

Before sharing a build, run:

```powershell
.\scripts\check-release-hygiene.ps1
```

This checks version text, VPK asset references, LiveArea PNG format, broken docs links, and accidental packaged audio files.

## Common Issues
- **Missing math symbols:** make sure `m` and `gcc` are linked.
- **`vita-elf-create` warnings:** these can be okay when using `libraries` in `exports.yml`.
- **Clock skew warnings under WSL:** When building from `/mnt/c`, Make can warn about timestamps a fraction of a second in the future. Use `bash scripts/test-host-wsl.sh` for host tests and `bash scripts/build-wsl.sh` for Vita artifacts; both build in the WSL filesystem cache.
- **GitHub Actions Vita build:** CI uses the official pinned `vitasdk/vitasdk` Docker image. Do not use `gnuton/vitasdk-docker:latest`; that image has broken before because its compiler required a newer glibc than the container provided.

## Install
1. Copy `eq_speaker.skprx` to `ur0:tai/`.
2. Add `ur0:tai/eq_speaker.skprx` to `ur0:tai/config.txt` under `*KERNEL`.
3. Reboot, install `EQVita.vpk`, and run.

## Presets

Current presets are stored as validated wrapper files:

- `ur0:data/eqvita/preset0.eqvp`
- `ur0:data/eqvita/preset1.eqvp`
- `ur0:data/eqvita/preset2.eqvp`

User Equalizer APO configs live in `ur0:data/eqvita/peq/`. On first launch the app copies its bundled `pch-1000.txt` there without overwriting an existing file.

Per-output assignments live in `ur0:data/eqvita/output-peq.eqpf`. Both the app and kernel plugin validate the wrapper and all three embedded controls before using it.

Legacy raw `preset%d.bin` files are imported read-only when no `.eqvp` file exists for the slot.

## Hardware Testing

Before publishing binaries or closing runtime issues, run [hardware-test-checklist.md](../testing/hardware-test-checklist.md).

For the full publish flow, use [release-checklist.md](../release/release-checklist.md).
