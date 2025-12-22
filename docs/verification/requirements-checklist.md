# Requirements Checklist

## Status
- **Pass:** Implemented and verified.
- **Fail:** Missing or incomplete.

## Items
1. **System-wide EQ:** **Pass** (Hooks `sceAudioOut`)
2. **Speaker-only gating:** **Partial** (Headphones detected; Bluetooth missing)
3. **No added latency:** **Pass** (In-place processing)
4. **44.1/48 kHz support:** **Pass** (Auto-detects sample rate)
5. **10-band EQ + Preamp:** **Pass** (31Hz - 16kHz)
6. **Instant updates:** **Pass** (Shared memory)
7. **Stability:** **Pass** (No heap/IO in hot path)
8. **Boot safety:** **Pending** (Plugin disabled by default)
9. **Install compatibility:** **Pass** (3.65 Ensō)

## Summary
- **Missing:** Bluetooth route detection.

