# App UI Notes

EQVita should feel like a small Vita settings app, not a debug screen.

This is the rough style guide for the whole app.

## The Vibe

Keep it simple:

- title at the top;
- short subtitle when it helps;
- rows for settings and actions;
- clear selected row;
- small icon on the left;
- value or arrow on the right;
- button help at the bottom.

If a screen can be a normal list, make it a normal list. Only use a custom layout when a list would be cramped or weird.

## Main Menu

The main menu should show what people use most:

- Equalizer;
- Simple EQ;
- Advanced EQ;
- Parametric EQ;
- Presets;
- Music Preview;
- Settings;
- Telemetry;
- Themes;
- Help.

Do not put random debug-looking values on the main menu. If something is only useful when fixing a bug, it belongs in Telemetry or the log.

## Rows

Most screens use rows.

Normal row types:

- read-only info;
- action button;
- adjustable value;
- link to another screen;
- small section divider.

Rows should stay predictable:

- icon left;
- label first;
- short helper text under it if needed;
- value, arrow, or checkmark on the right.

If a row is not selected, do not make it look like it is already being pressed.

## Simple EQ

Simple EQ is for quick tuning.

Keep it focused on:

- preamp;
- bass;
- mids;
- treble;
- preset save controls.

Changes apply live. Saving writes the sound into a preset slot.

## Advanced EQ

Advanced EQ is for people who want the full curve.

Show:

- preamp;
- all 10 graphic-EQ bands, or all ordered operations in an imported Equalizer APO config;
- the L/R channel scope, filter type, frequency, Q/S, gain, and `Copy` matrix for imported operations;
- preset save controls.

It should still feel like part of the app, not like a hidden debug page.

## Presets

Presets are saved sound profiles.

Make it clear which slot is selected, what can be loaded, and what can be saved.

Equalizer APO import is a first-class `Parametric EQ` item immediately below `Advanced EQ` on the main menu. `Presets` retains a `Parametric EQ` shortcut to the same screen. The child screen has one adjustable output selector plus assigned-file, choose, inspect/edit, clear, live-route, and assignment-count rows. This keeps three device profiles understandable without showing three near-identical file pickers.

The `.txt` browser is rooted at `ur0:data/eqvita/peq/` and reports a useful file/line error. `Inspect / edit PEQ` opens the selected output in Advanced EQ. Output assignments persist separately from numbered preset slots. PEQ is an exclusive mode: graphic bands and Bass guard are off and APO Exact is locked until the selected output switches back to Simple EQ.

Built-in presets are only a starting point. Different Vitas, headphones, Bluetooth speakers, and speaker mods can all react differently.

## Settings

Settings is for normal app behavior:

- speaker-only or all outputs;
- HPF / Bass guard;
- headroom;
- startup behavior;
- future app options.

Deep port details do not belong here. Put them in Telemetry.

## Telemetry

Telemetry is where status and bug-report stuff lives.

Good things to show here:

- route;
- bypass reason;
- ABI state;
- clip counter;
- port state;
- timing/status hints.

Use plain labels first. Technical details are fine here, but do not make it unreadable.

## Themes

Themes are just skins.

They can change colors and icons, but they should not change layout. Every theme needs readable text and a strong selected row.

## Help

Help should explain the app without becoming a manual.

Good topics:

- what EQVita does;
- presets;
- Simple EQ;
- Advanced EQ;
- preamp;
- HPF / Bass guard;
- speaker-only mode;
- all-output mode;
- bypass;
- where the log is;
- GitHub repo link.

Keep each item short. If it needs a paragraph, it probably belongs in the README.

## Music Preview

Music Preview gets a custom layout because it needs more than a plain list.

It is only for tuning EQ with a local song. It is not trying to become a full music player.

Keep it focused on:

- choose one file;
- play, pause, stop, loop;
- preview volume;
- active preset slot;
- output mode;
- preamp and headroom;
- responsive controls while music plays.

Do not add playlists or a music library unless there is a real reason later.

## Dialogs

Dialogs should look like the rest of the app.

Use:

- themed icon on each option;
- selected-row highlight;
- checkmark on the selected option when useful;
- short labels.

The unsaved-EQ prompt uses:

- `Save to this slot`;
- `Discard and leave`;
- `Keep editing`.

## Text

Write like a normal person.

Good:

- `Choose file`;
- `Pick OGG, MP3, or WAV`;
- `Preview player volume`;
- `Save this sound profile`;
- `Turn sound tuning on or off`;
- `Output scope and safety`.

Avoid long internal terms on normal screens. Save the nerd stuff for Help, Telemetry, logs, or docs.

## Controls

Keep controls the same across screens:

- D-pad and left stick move;
- Left/Right adjust;
- L/R adjust EQ faster;
- Cross selects;
- Circle goes back;
- Start toggles EQ;
- Triangle opens Help;
- touch taps rows and drags lists.

File picker back behavior:

- Circle inside a folder goes up one folder;
- Circle at `ux0:` or `ur0:` goes back to the storage list;
- Circle from the storage list goes back to Music Preview.

New screens should use the shared navigation helpers where possible.
