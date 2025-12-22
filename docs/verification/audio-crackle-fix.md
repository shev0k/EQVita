# Audio Crackle Fix Verification

## Issues & Fixes

### 1. Filter State Reset
**Issue:** Filter state (`z1`, `z2`) was reset on parameter updates, causing clicks.
**Fix:** `eq_dsp_apply` now preserves filter state when updating coefficients.

### 2. Incorrect RBJ Formula
**Issue:** `biquad_compute` used linear gain ($10^{dB/20}$) instead of $\sqrt{10^{dB/20}}$ for the $A$ parameter.
**Fix:** Updated to calculate $A = \sqrt{gain}$.

### 3. Denormals
**Issue:** Small floating point numbers caused CPU spikes.
**Fix:** Added check in `process_biquad` to flush denormals to zero.

### 4. High Frequency Stability
**Issue:** High frequencies near Nyquist caused instability.
**Fix:** Clamped center frequency to `sample_rate / 2 - 100`.

### 5. Incorrect Gain Calculation
**Issue:** `mdB_to_gain` used wrong divisor, causing 10x gain.
**Fix:** Updated divisor to `20000.0f`.

## Verification

1.  **Build & Install:**
    ```bash
    cd eq-master && mkdir build && cd build
    cmake .. && make
    ```
    Copy `plugin/eq_speaker.skprx` to `ur0:tai/`.

2.  **Test:**
    *   **Preamp:** Verify clarity at -6dB, -12dB, and 0dB.
    *   **EQ Bands:** Move sliders; ensure no crackling.
    *   **Extreme Settings:** Verify no distortion at +6dB.
    *   **Games:** Test with games and system sounds.


New debug counters are available in `eq_status_t`:
*   `debug_port`: Last audio port ID used.
*   `debug_len`: Number of frames processed in last call.
*   `debug_channels`: Number of channels processed.
*   `debug_run_count`: Total number of hook executions.

Monitor these values to ensure `debug_len` matches expected buffer sizes (e.g., 256, 512, 1024) and `debug_channels` is correct (usually 2).
