# Functional capability gate

Protect **working capabilities**, not games. Games serve as integration witnesses.
No 61-game baseline; no contract for known-broken boots. Existing FP/DC43 baseline untouched.

```powershell
python tools/capability-gate/capability_gate.py --build-dir C:\Users\Lucas\source\repos\zeebo_libretro\build-fp-gate
```

Requires existing Release `libretro_smoke.exe`, `lozeebo_libretro.dll`, local ROMs at
`D:\Games\Zeebo - Zeebo`. No build, extra dependencies, copying copyrighted assets or baseline updates.
`--rom-dir`, `--output`, `--filter "Double Dragon"`, `--workers 1` supported; maximum two workers.
Default 180-second wall budget per witness, Windows external `taskkill /T /F` on timeout.
No machine-speed threshold in evidence. Each run creates unique logs/PPMs/report.json/REPORT.md.
Exit 0 only if all selected functional witnesses pass; missing/different ROM = INCONCLUSIVE, exit 1.
Changed deterministic snapshot = regression/review, never silently rebaseline. Witnesses with unverified source reproducibility assert nonuniform rendering/audio only; no arbitrary accepted-hash list. Errors compare additions only.

## Capability inventory / ownership

| Capability | Existing/new test | Fixture | Limit |
|---|---|---|---|
| Complete ZIP/7z load, assets, MIF siblings, cache identity/hit, unsafe entries | Existing `tests/check-content-packages.py`, `tests/content_package_check.c` | Synthetic archives; local real packages | Own build path in existing runner; aggregator integrates, not duplicated here |
| Frontend content load / `.run` path | Existing `tests/check-content-frontend.py`, `tests/content_run7_check.c`, `tools/fp-gate/check-content-path.ps1` | Synthetic frontend; Family Pack | Existing tests own archive/load assertions |
| Press/release/rearm transitions | Existing `tests/input_transitions.c`, `tests/fp_position_transitions.py` | Synthetic state; Family Pack | Not implied by no-input boot witnesses |
| Direction transitions | Existing `tools/dd-gate/check-directions.ps1` | Double Dragon | Not duplicated |
| Audio ADPCM rate | Existing `tests/audio_adpcm_rate.c` | Synthetic samples | Boot audio counts do not establish rate correctness |
| Render nonuniform output / palette snapshot | **New** `capability_gate.py` | 13 nonuniform evidence fixtures below | 180 frames without input; no semantic menu/gameplay claim; exact SHA only after repeated evidence validation |
| Audio nonzero production floor | **New**, same runner | Double Dragon, Pac-Mania, Um Jogo de Ovos, Zeebo Extreme Rolima | Respective source floors 50528 / 6544 / 3902 / 37884; no assertion that silent boot fixtures lack audio capability |
| Comparator red/green | **New** `test_comparator.py` | Synthetic dictionaries and PPMs | Does not load emulator |

Menu semantics/gameplay remain owned by existing `tools/fp-minigame-proof-v2`,
`tools/fp-final-playable-proof`, `tools/ovos-gate`, `tools/dd-gate`.
Main session owns global `tools/regression.py`; this runner is a single subprocess integration point. Interface: invoke Python script with `--build-dir <build or Release directory>`; optional `--rom-dir`, `--filter`, `--output`; consume stdout `{counts, report}` and report JSON `results[].{fixture,capabilities,status,reasons,observed}`. Known source-snapshot limits appear in `limits`; consumers must not infer palette coverage for those witnesses.

## Evidence

`evidence180.json` contains only 13 nonuniform render witnesses:
Bejeweled Twist, Double Dragon, Iron Sight, Pac-Mania, Reckless Racing,
Um Jogo de Ovos, Zeebo Extreme Baja, Boia Cross, Corrida Aerea, Jetboard,
Rolima, Zeebo Family Pack, Zuma's Revenge.

Initial provenance: existing main `artifacts/smoke-all` logs/final PPMs,
180 frames, no input, commit `bbc756c98bbc930b2d776257a030d3225127b378`.
SHA256 current source-build DLL, smoke executable, ROMs, logs and frames recorded; historical logs do not independently embed DLL identity. Original source exit
0 inferred from complete summary, checked independently by actual runs.
Visual evidence: `C:\Users\Lucas\source\repos\zeebo_libretro\artifacts\smoke-all\contact-sheet.png`.
No binaries/PPMs/ROMs tracked. Extended survey uses different capture lengths,
so does not override these 180-frame witnesses.

Baseline immutable during runs. Every subprocess removes inherited `ZEEMU_SMOKE_*`.
Two independent runs reproduced all 13 nonuniform renders and audio floors. Ten snapshots matched source exactly. Bejeweled Twist, Um Jogo de Ovos, Zuma's Revenge matched each other across both clean-environment runs but differed from historical source; historical capture environment is not established. Their source hashes remain recorded, **not** replaced: snapshot capability unverified, only nonuniform rendering/audio asserted. `snapshot_deterministic=false` means source-to-clean-run reproducibility unproven, not proven random behavior.

No known-broken fixture, historical timeout, expected unsupported failure becomes PASS.
Missing ROMs never green. Disappearance of an old error never regresses.
No claim of all-game compatibility or gameplay from a boot image.

```powershell
python tools/capability-gate/test_comparator.py
```
