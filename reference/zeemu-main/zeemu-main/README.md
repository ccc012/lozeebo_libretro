# Zeemu

Zeemu is an experimental Zeebo console emulator.

The project focuses on documenting and recreating the Zeebo runtime environment:
BREW services, module loading, virtual filesystem behavior, input, audio,
software rendering, and OpenGL ES-style graphics paths. It is a work in
progress and should be treated as an emulator research project, not a finished
compatibility layer.

## Status

Zeemu can boot and run a growing subset of Zeebo software, but compatibility is
incomplete. Many titles still depend on partially implemented BREW APIs,
graphics state, audio paths, filesystem behavior, timing, or CPU instructions.

The public repository does not include games, firmware dumps, SDK files, logs,
or other proprietary material.

## Legal

Zeemu does not contain or distribute Zeebo games, firmware, SDKs, or copyrighted
content. Use only software and data that you are legally allowed to use.

This project is not affiliated with Zeebo Inc., Qualcomm, Tectoy, or any game
publisher.

## Requirements

- CMake 3.10 or newer
- A C++17 compiler
- SDL3, vendored under `third_party/SDL3`
- Optional FFmpeg libraries (`libavformat`, `libavcodec`, `libavutil`) for some
  media decode paths

The current development environment is Windows with GCC/w64devkit, but the code
is kept as portable C++17 where practical.

## Build

Prebuilt executables are not distributed. Build Zeemu from source:

```powershell
cmake -S . -B build
cmake --build build --target ZeemuApp -j 14
```

The main executable is emitted at the repository root:

```powershell
.\Zeemu.exe
```

On non-Windows platforms the executable name and build generator may differ.

## Usage

Launch the GUI:

```powershell
.\Zeemu.exe
```

Run a BREW module directly:

```powershell
.\Zeemu.exe run-app path\to\app.mod [clsid]
```

Run with verbose guest debug output suppressed:

```powershell
.\Zeemu.exe run-app-fast path\to\app.mod [clsid]
```

Run a configured smoke target:

```powershell
.\Zeemu.exe smoke <target> [seconds] [fast|normal]
```

Smoke targets require matching local test data and are mainly intended for
development and regression checks.

## Default Controls

Keyboard:

```text
Arrow keys       D-pad
Enter / Space    Select
Esc / Backspace  Clear / Back
Home / H         Home
1 / Z            Button 1
2 / X            Button 2
3 / C            Button 3
4 / V            Button 4
Q / E            Left / right upper shoulder
LShift / RShift  Left / right lower shoulder
WASD             Left thumbstick
IJKL             Right thumbstick
```

SDL gamepads use the standard layout where the D-pad and left stick provide
directional input, face buttons map to Zeebo buttons 1-4, shoulders/triggers
map to the shoulder buttons, and the right stick is exposed as the right
thumbstick.

## Installing Games

Zeemu does not provide games. If you have a legally obtained Zeebo game package,
extract it under `roms/` while keeping the official numeric title ID as the
directory name. Keep the package layout as extracted; Zeemu scans the installed
apps and discovers the module paths from the package metadata.

Expected layout:

```text
roms/
  123456/
    ...
```

For example, a game whose official ID is `123456` should stay under
`roms/123456/`. Do not rename title folders to friendly names; the emulator and
catalog tooling expect the original IDs.

## Repository Layout

- `brew/` - BREW HLE services and platform interfaces
- `cpu/` - ARM CPU core, memory, and disassembly support
- `frontend/` - SDL/ImGui launcher
- `graphics/` - SDL-backed presentation and rendering helpers
- `loaders/` - BREW module and executable loading
- `runtime/` - app runner, CLI commands, callbacks, and guest execution glue
- `tools/` - local inspection and debugging utilities
- `vfs/` - virtual filesystem mapping
- `third_party/` - vendored third-party dependencies

## Development Notes

Zeemu is built by validating behavior against local traces, SDK-shaped API
contracts, and real guest code paths. Compatibility fixes should model shared
platform behavior rather than adding per-game shortcuts.

When reporting bugs, include the command used, the module or target name, and
the relevant stdout/stderr log if available. Do not attach copyrighted game
content.

## License

Zeemu is licensed under the GNU General Public License v3.0. See
[`LICENSE`](LICENSE).
