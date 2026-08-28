# Using Equalizer APO Configs

EQVita can use a practical stereo subset of Equalizer APO `.txt` files. Copy a config into `ur0:data/eqvita/peq/`, choose an output in `Parametric EQ`, then select the file.

If a line cannot be represented safely on the Vita, the whole import fails with a file and line error. EQVita does not quietly approximate active commands.

The full Windows format is documented in the [official Equalizer APO configuration reference](https://sourceforge.net/p/equalizerapo/wiki/Configuration%20reference/). EQVita supports only the pieces listed here.

## What Works

- `Preamp: <value> dB`. Multiple stereo preamps add together.
- Numbered or unnumbered `Filter` lines starting with `ON` or `OFF`.
- Peak filters: `PK` and `PEQ` with `Fc`, `Gain`, and `Q`.
- Shelves: `LS`, `LSC`, `HS`, and `HSC` with `Fc`, `Gain`, and optional `Q` or a `dB` slope.
- `Channel: L`, `R`, `1`, `2`, or `ALL`. Commas and spaces are accepted.
- Ordered stereo `Copy` expressions made from L/R terms and numeric coefficients.
- Relative or absolute `Include` paths that stay under an allowed data root.
- `#` comments, parenthesized trailing notes, decimal commas, and UTF-8 BOMs.

`Device` lines are ignored. Windows device names do not map to Vita outputs; choose the Vita route in the app instead.

An `OFF` filter is skipped. A comment or line without `Command: parameters` is also skipped.

### Copy Examples

These are supported:

```text
Copy: L=R R=L
Copy: L=-1*L R=-1*R
Copy: L=0.5*L+0.5*R R=0.5*L+0.5*R
Copy: R=L
```

Copy operations stay in file order with the filters around them. Coefficients may use `+`, `-`, and an optional `*`.

Constant output values, dB-valued coefficients, and channels other than L/R are not supported.

## What Does Not Work

EQVita rejects active commands it does not understand. That includes:

- `GraphicEQ`, convolution, delay, and custom IIR coefficients;
- low-pass, high-pass, band-pass, notch, and all-pass filters;
- `Stage`, `If`, `Else`, `Eval`, variables, and inline expressions;
- `BW Oct` filter shapes;
- channel-scoped preamps;
- surround channels such as C, LFE, RL, and RR.

Fix or remove the unsupported line and import again. A failed import leaves the current profile untouched.

## Limits

| Item | Limit |
| --- | --- |
| Active Filter + Copy operations | 48 |
| Frequency | 1 to 24,000 Hz |
| Filter gain | -24 to +24 dB |
| Combined preamp | -30 to +12 dB |
| Q or S value | 0.01 to 100 |
| Shelf slope | 0.12 to 12 dB/octave |
| Copy coefficient | -4 to +4 |
| Line length | 1,023 bytes |
| Canonical path length | 511 bytes |
| One file | 256 KiB |
| Include chain | 8 files, including the root file |
| Documents read | 32, including the root and repeated includes |
| Total input | 512 KiB |

Frequencies above the current stream's Nyquist limit are moved to a stable value below Nyquist while audio is running.

## Includes And Paths

The app allows imports under `ur0:data/` and `ux0:data/`. The file picker starts in `ur0:data/eqvita/peq/`, but a config may include another file under either allowed data root.

Both `/` and `\` separators work. Duplicate separators, `.`, and `..` are normalized before a file is opened. Parent traversal is fine when the final path stays inside an allowed root. Mount changes, root escapes, prefix tricks such as `ur0:data_evil`, and canonical include cycles are rejected.

Cycle checks use the normalized path, so this still counts as a cycle:

```text
Include: sub/../config.txt
```

Repeated includes are allowed, but every read counts toward the document and total-byte limits.

## Routes And Saving

Each imported config is assigned to Vita speakers, wired headphones, or Bluetooth. You can assign different files to all three.

Once any output profile exists, unassigned outputs are bypassed. Clear the final assignment to return to the normal speaker-only/all-output setup.

Assignments and curves are stored in `ur0:data/eqvita/output-peq.eqpf`. They survive reboot without using a numbered preset slot. Switching an assigned route to Simple EQ, reset, or a graphic preset clears its stored source filename.

Stereo channel filters and Copy matrices use the left and right paths. On a mono Vita stream, EQVita keeps the left-channel path.

## APO Exact And Clipping

Imported files run in `APO Exact`. The config's preamp and ordered operations are applied without the automatic headroom reduction used by normal EQ modes.

At the final signed 16-bit output boundary:

- Safe, Loud, and Direct use the normal soft-knee limiter.
- APO Exact uses hard saturation.
- Both modes count samples that crossed the 16-bit range in clipping telemetry.

Hard saturation is intentional for exact imports, but it can sound harsh. Start with the preamp recommended by the profile. If telemetry rises or the audio sounds rough, lower `Preamp` in the file.

## Troubleshooting

**The import shows a file and line error.** Open that file, fix the named line, and try again. No part of the failed config was applied.

**An include cannot be read.** Check its spelling, separator, and location. It must resolve under `ur0:data/` or `ux0:data/` and stay within the file and byte budgets.

**The right profile does not play.** Open EQVita so it can refresh the current route. Confirm the output has an assigned profile; unassigned routes are deliberately bypassed.

**The profile clips.** Lower its preamp. APO Exact hard-clips at the PCM boundary instead of using the normal soft limiter.

For hardware limits and route caveats, see [Known Limits](known-limits.md).
