# Third-Party and Reuse Notes

This project is being documented as GPLv3.

Some parts of the work may be based on, adapted from, or inspired by other projects and references used to accelerate development. Keep this file updated as we identify each source.

## Current references mentioned in the project
- Infuse
- Zeemu
- Other compatible references that may be added later

## Recorded use
- Zeemu (`C:\Users\Lucas\source\repos\zeemu_reference`): used as a GPL-3.0 reference for BREW `IShell` behavior, service CLSIDs, applet CLSIDs, timer/resource handling expectations, and compatibility stubs needed by real Zeebo games such as Pac-Mania.
- Zeemu upstream (`https://github.com/mrpostiga/zeemu`, commit `ebd1b20`): GPL-3.0 reference used for `AEEApplet`, `IHID`, `ISignal`, `IFileMgr`, `IDisplay` and device-bitmap behavior required by Zeebo Family Pack.
- Infuse (`https://tuxality.net/projects/infuse_zeebo_emulator`): behavior and compatibility reference only. Its public GitHub repository does not contain emulator source code and its custom license is not compatible with copying code into this GPLv3 project. No Infuse code or binary is redistributed here.
- GGZ BREW Tools (`ggzbrewtools-main`, Tuxality, 2021): source is under a permissive zlib-style license and may be adapted into GPLv3 tools. It documents the big-endian GGZ entry table and concatenated GZIP member layout used by some BREW/Zeebo games.
- Noto Sans JP (`brew/font/Noto Sans JP 400.otf` inside the local Infuse package): independently licensed under SIL OFL 1.1. It is eligible for optional font rendering if the OFL notice remains with the font.
- GMGSx (`brew/soundfont/GMGSx.sf2` inside the local Infuse package): identified by the package notice as public domain. It is eligible as an optional MIDI soundfont, but is not currently copied into the core.
- GeneralUser GS (`brew/soundfont/GeneralUser GS v1.471.sf2`): separately licensed, but the complete GeneralUser license text is not present beside the asset. Do not redistribute it until that license and its notice requirements are added and verified.
- Infuse UI images and `Infuse.exe`: not reusable or redistributable by this project. `sb_chasingdaylight.mp3` is identified as CC-BY 4.0 but is unrelated to emulation and is not adopted.

## What to record for each source
- Project name
- Repository or source link
- What was reused or adapted
- License terms
- Any required attribution or notice

## Working rule
- If code, assets, or documentation are copied or adapted, record it here as soon as it is added.
- If a source is only used as inspiration, it can still be noted here for transparency.
