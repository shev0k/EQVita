# EQ Speaker for PS Vita

System-wide 10-band graphic equalizer kernel plugin for PS Vita (3.65 Ensō) with a companion UI app.

## Layout
- `plugin/` — Kernel plugin (`eq_speaker.skprx`)
- `app/` — UI app (`EQVita.vpk`)
- `common/` — Shared headers
- `docs/` — Technical notes

## Build (VitaSDK)
```bash
export VITASDK=/usr/local/vitasdk
cmake -S . -B build
cmake --build build
```
Outputs:
- `build/plugin/eq_speaker.skprx`
- `build/app/EQVita.vpk`

## Install
1. Copy `eq_speaker.skprx` to `ur0:tai/`.
2. Add to `ur0:tai/config.txt` under `*KERNEL`:
   ```
   *KERNEL
   ur0:tai/eq_speaker.skprx
   ```
3. Reboot.
4. Install `EQVita.vpk`.
5. Run EQVita to adjust settings.

**Note:** The plugin is disabled by default to prevent boot issues. Launch EQVita to enable it.

## Usage
- **Bands:** 31Hz - 16kHz
- **Gain:** ±12 dB
- **Controls:**
  - Triangle: Save preset
  - Square: Load preset (`ux0:data/eqvita/preset0.bin`)
- **Status:** Shows route, sample rate, and clip count.

## Notes
- Bluetooth detection is not yet implemented.
- DSP uses an in-place biquad chain with smoothing.
- If audio crackles, reduce gain or preamp.

