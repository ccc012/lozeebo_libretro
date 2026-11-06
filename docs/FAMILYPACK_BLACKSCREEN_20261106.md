# Family Pack visual regression — 2026-11-06

## Symptom
RetroArch / short smoke: black screen. `video_varied=1` can lie (noise / partial). Honest PPM must show real menu (white + orange accents), not solid / gradient.

## Root causes (confirmed vs evidence)

1. **CreateInstance stall** — FP poll loop needs enough guest instructions (~60f @ 1M instr/frame on current core). Older/short runs exited mid-CI → black FB (`video_first=00000000`).
2. **Autopresent during CreateInstance** — `zegl_autopresent()` → `eglSwapBuffers` clobber R0; poll at `0xE94` slides to `0x04000000`. Guard in `libretro_core.c`: only when `zboot_state()==ZBOOT_RUNNING`.
3. **IDisplay real GetDeviceBitmap** — IDIB must use stub QI vtbl + `pPaletteMap=+4=0`, depth **32 bits**, pitch `640*4`. Wrong layout → CI abort / no EGL path. `idisplay_real.c` case 16.
4. **Fake HandleEvent(evt=2)=SUSPEND** — must **not** register for CLSID `0x010903C6`. Current `boot.c` excludes FP/RE4/Zuma.
5. **Stale RetroArch core** — cores dir hash differed from working tree; black despite source OK.

## Working evidence
- `fp_honest_menu.ppm` / `fp_repro_fresh.ppm`: white menu, accents `FE9811`/`014466`, `video_varied=1`, xor!=0
- Log: `CreateInstance retornou 0`, `EVT_APP_START`, `DrawArrays[0]`
- Smoke: `build-fpfix\Release\libretro_smoke.exe` + `lozeebo_libretro.dll` + `real_family_pack_game.mod` 60–300f

## Deploy
- `D:\SteamLibrary\steamapps\common\RetroArch\cores\{lozeebo,zeebo}_libretro.dll` ← `build-fpfix\Release\lozeebo_libretro.dll`
- Reload core in RetroArch (close content / restart)

## Not a fix
Do not paint gradient / test_quad / solid yellow to fake `video_varied`.
