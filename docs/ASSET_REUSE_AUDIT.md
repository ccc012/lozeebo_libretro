# Asset and Tool Reuse Audit

Audit date: 2026-07-07

## Infuse A1 Local Package

The local directory is useful as documentation and as a source of separately
licensed third-party assets. The Infuse package itself has a custom,
non-commercial, no-unofficial-redistribution license. The directory is ignored
by Git so the package and `Infuse.exe` cannot be published accidentally.

| Item | License status | Useful for | Decision |
| --- | --- | --- | --- |
| `Infuse.exe` | Infuse custom license | Reference execution only | Never link, copy, reverse engineer, or redistribute |
| `README.md` | Infuse package | Installation layout, controller mapping, compatibility facts | Use factual behavior as reference; do not copy wholesale |
| `LICENSE` | Infuse package | Compliance record | Keep only in the local package |
| Noto Sans JP OTF | SIL OFL 1.1 | BREW font rendering and Japanese glyphs | Reusable with OFL notice; not needed for current Family Pack boot |
| `GMGSx.sf2` | Public domain per package notice | MIDI synthesis | Reusable optional soundfont after a MIDI renderer exists |
| GeneralUser GS SF2 | GeneralUser GS License 2.0 | Higher quality MIDI synthesis | Hold until full license text and attribution requirements are verified |
| `sb_chasingdaylight.mp3` | CC-BY 4.0 per package notice | Infuse menu music only | Not relevant; do not adopt |
| Infuse UI PNG files | No separate permissive license identified | Infuse user interface | Do not adopt |

Useful factual information confirmed by the README:

- Zeebo content layout uses `brew/mif/<id>.mif` and `brew/mod/<id>/`.
- A same-name `.sig` file is required by Infuse, but its content is not
  validated. This is an Infuse behavior, not necessarily a Zeebo requirement.
- Zeebo input exposes four face buttons, Home, Clear, D-pad, two triggers and
  two analog sticks, with up to two controllers.
- MIDI playback uses selectable `.sf2` soundfonts.

## GGZ BREW Tools

`ggzbrewtools-main` is source code under a permissive zlib-style license. It is
compatible with this GPLv3 project as long as its copyright and license notice
remain in adapted source files.

The code documents this GGZ container structure:

1. A big-endian table of `(offset, original_size)` 32-bit pairs.
2. The table ends when the first GZIP member is reached.
3. Every entry is a concatenated GZIP stream.
4. The GZIP filename is used when present; unnamed members receive an ordinal.

Recommended use:

- Keep the original pack/unpack utility as a developer tool.
- Later add a bounded, read-only GGZ parser for diagnostics or asset inspection.
- Do not add Boost to the LibRetro core solely for GGZ. Runtime games normally
  read their own `.ggz` files through `IFileMgr`.
- This format does not parse the Family Pack `data.vfs`; a separate VFS parser
  is required for that title.

## Current Priority

None of these files replaces the missing EGL/OpenGL ES implementation that
currently blocks Family Pack rendering. The most useful immediate source for
that work remains the GPLv3 Zeemu implementation.
