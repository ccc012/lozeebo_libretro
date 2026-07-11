# Third-Party and Reuse Notes

This project is being documented as GPLv3.

Some parts of the work may be based on, adapted from, or inspired by other projects and references used to accelerate development. Keep this file updated as we identify each source. This is also the single canonical tracker for third-party asset licenses and reuse decisions — record the decision here as soon as a new asset or reference is identified, and update it if a decision changes (e.g. a license gets verified).

## Current references mentioned in the project
- Infuse
- Zeemu
- GGZ BREW Tools
- Other compatible references that may be added later

## Recorded use and per-asset decisions

| Asset / source | License | Decision / status |
| --- | --- | --- |
| Zeemu | GPL-3.0 | Used as a reference only for BREW `IShell` behavior, service/applet CLSIDs, timer/resource handling expectations, and compatibility stubs needed by real Zeebo games such as Pac-Mania. Behavior is reimplemented independently, not copied. |
| Zeemu upstream (`https://github.com/mrpostiga/zeemu`, commit `ebd1b20`) | GPL-3.0 | Reference used for `AEEApplet`, `IHID`, `ISignal`, `IFileMgr`, `IDisplay` and device-bitmap behavior required by Zeebo Family Pack. |
| Infuse (`https://tuxality.net/projects/infuse_zeebo_emulator`) | Infuse custom, non-commercial, no-unofficial-redistribution license | Behavior and compatibility reference only. Its public GitHub repository does not contain emulator source code and its custom license is not compatible with copying code into this GPLv3 project. The local package (`Infuse.exe`, binary, and bundled assets) was kept in the working tree temporarily and has since been **removed from the repository entirely** after extracting the compatibility facts listed below; it was never pushed to the public remote. Do not re-add the package, or `Infuse.exe` itself, under this license: never link, copy, reverse engineer, or redistribute it. |
| GGZ BREW Tools (Tuxality, 2021) | Permissive zlib-style license | Compatible with this GPLv3 project as long as its copyright and license notice remain in adapted source files. Documents the big-endian GGZ entry table and concatenated GZIP member layout used by some BREW/Zeebo games. |
| Noto Sans JP (`brew/font/Noto Sans JP 400.otf`, formerly inside the local Infuse package) | SIL OFL 1.1 | Independently licensed. Eligible for optional font rendering if the OFL notice is kept with the font; not needed for current Family Pack boot and not currently copied into the core. |
| GMGSx (`brew/soundfont/GMGSx.sf2`, formerly inside the local Infuse package) | Public domain per the package's own notice | Eligible as an optional MIDI soundfont once a MIDI renderer exists; not currently copied into the core. |
| GeneralUser GS (`brew/soundfont/GeneralUser GS v1.471.sf2`) | GeneralUser GS License 2.0 | Hold. The complete license text is not present beside the asset. Do not redistribute it until that license and its notice requirements are located and verified. |
| Infuse UI images | No separate permissive license identified | Not reusable or redistributable by this project; do not adopt. |
| `sb_chasingdaylight.mp3` (Infuse menu music) | CC-BY 4.0 per package notice | Unrelated to emulation; not adopted. |
| OpenZeebo | See upstream repo | Real-hardware reverse-engineering tools (JTAG, NAND, bootloader upload). Kept as a reference for hardware-accurate behavior; not currently wired into the core. |
| zeebo_doom | See upstream repo | A homebrew Doom port for Zeebo; kept as an example of another BREW-based app running on the real console. |

### Facts documented from Infuse's public README (not redistributed here)
These are plain compatibility facts observed from Infuse's public README, kept for reference now that the package itself has been removed from the repo:
- Zeebo content layout uses `brew/mif/<id>.mif` and `brew/mod/<id>/`.
- A same-name `.sig` file is required alongside content by Infuse, but its content is not validated. This is an Infuse behavior, not necessarily a Zeebo requirement.
- Zeebo input exposes four face buttons, Home, Clear, D-pad, two triggers, and two analog sticks, with up to two controllers.
- MIDI playback uses selectable `.sf2` soundfonts.

### GGZ container format facts (from GGZ BREW Tools)
- A big-endian table of `(offset, original_size)` 32-bit pairs.
- The table ends when the first GZIP member is reached.
- Every entry is a concatenated GZIP stream.
- The GZIP filename is used when present; unnamed members receive an ordinal.

Recommended use of GGZ BREW Tools:
- Keep the original pack/unpack utility as a developer tool.
- Later add a bounded, read-only GGZ parser for diagnostics or asset inspection.
- Do not add Boost to the LibRetro core solely for GGZ. Runtime games normally read their own `.ggz` files through `IFileMgr`.
- This format does not parse the Family Pack `data.vfs`; a separate VFS parser is required for that title.

## What to record for each source
- Project name
- Repository or source link
- What was reused or adapted
- License terms
- Any required attribution or notice

## Working rule
- If code, assets, or documentation are copied or adapted, record it here as soon as it is added.
- If a source is only used as inspiration, it can still be noted here for transparency.
