# Build Instructions (VitaSDK)

## Prerequisites
- VitaSDK (`VITASDK` env set)
- CMake ≥3.5, make (or ninja)

## Build
```bash
export VITASDK=/usr/local/vitasdk
cmake -S . -B build
cmake --build build
```
**Artifacts:**
- `build/plugin/eq_speaker.skprx`
- `build/app/EQVita.vpk`

## Common Issues
- **Missing math symbols:** Ensure `m` and `gcc` are linked.
- **`vita-elf-create` warnings:** Ignore if using `libraries` in `exports.yml`.

## Install
1. Copy `eq_speaker.skprx` to `ur0:tai/`.
2. Add `ur0:tai/eq_speaker.skprx` to `ur0:tai/config.txt` under `*KERNEL`.
3. Reboot, install `EQVita.vpk`, and run.

