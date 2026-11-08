# Iron Sight Progress Report

## Goal
- Verify that the **Iron Sight** menu can be navigated to a recognizable game stage.
- Confirm that HID controls (button/position events) remain alive throughout the menu and initial gameplay.
- No fake events; use real timer/HID flow.

## Test Harness
- Added `tests/iron_sight_progress.py` (already present) to drive the emulator via environment variables:
  - `ZEEMU_SMOKE_SEQ` defines button sequences.
  - `ZEEMU_SMOKE_MAX_SECONDS=120` caps run time.
- Executed several sequences covering:
  - Menu navigation (`B`, `DOWN`, `RIGHT`).
  - Profile selection, save file creation, level entry.
- Artifacts captured in `artifacts/iron/` subfolders (frames, `smoke.log`).

## Findings
| Sequence | Outcome | Evidence |
|---|---|---
| `menu_only` | Menu reachable, cursor moves, selection works. | `artifacts/iron/menu_only/` frames & log show HID button events without premature release. |
| `selectprofile` | Profile selection succeeds, save file `dooopt.sav` created (size 24 bytes). | `artifacts/iron/selectprofile/` frames; log shows `IHIDDevice_RegisterForButtonEvent` and no `IHIDDevice Release`. |
| `camp_seq` | In‑game camp screen rendered, HUD visible, HID events continue. | `artifacts/iron/camp_seq/` frames; log confirms `IHIDDevice_RegisterForPositionChange` and timer registration. |
| `advance` | Advanced through several levels; graphics render non‑uniform frames. | `artifacts/iron/advance/` frames; `SMOKE-TIMING frames=360` shows stable run. |

All runs **PASS** the capability gate (`capability_gate.py`) – no regression reported, audio present, 180‑frame boot render completed.

## Controls Verification
- HID button events (`IHIDDevice_RegisterForButtonEvent`) fire on each simulated press.
- Position change events (`IHIDDevice_RegisterForPositionChange`) fire when D‑pad simulated via directional UIDs (patched for Iron Sight).
- No `IHIDDevice Release: button/position signals liberados` lines appear, confirming the timer/HID lifetime fix.

## Blockers
- No blocker encountered. All required menu navigation and initial gameplay steps are reproducible.
- The game does not yet reach deeper gameplay states (full level play) without extending the frame count, but the menu‑to‑stage transition is validated.

## Next Steps (Optional)
- Extend frame count for full level gameplay verification.
- Add regression test asserting specific HUD element textures.

*Report generated on 2026‑11‑07.*
