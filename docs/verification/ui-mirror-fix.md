# EQVita UI Mirror Fix

## Issue
`ui_init()` called `psvDebugScreenInit()` twice, causing font bits to flip back and mirror the text.

## Fix
Removed the second initialization in `ui_init()`.

## Verification
1. Build and install `EQVita.vpk`.
2. Launch app.
3. Confirm text is readable (left-to-right).

