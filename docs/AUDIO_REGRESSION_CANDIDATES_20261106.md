# Candidatos Alice / Sports / Ridge / Foot Camp / Family Pack — 2026-11-06

## Correções transversais (com evidência)

| Mudança | Arquivo | Evidência |
|---|---|---|
| `ZMEDIA_MAX` 8 → 64 | `src/brew/imedia.c` | Alice: 8 ok + 20 `CreateInstance→0`. Foot Camp: 32 ok + 29 fail. Após 64: Foot 61 ok / 0 fail; Alice 28 ok / 0 fail. |
| `LoadResDataEx` embutido só Pac-Mania (`0x01087B72`); demais `.bar` via `zbar_parse_file` | `src/brew/boot.c` | Ridge CLSID `0x01087B73` recebia `size=4744` de `0x23128` (código ARM). Guest usava `dwSize=0x10005AE8`. Após fix: MP3 real do `ridgeracer.bar`, `audio_nonzero=881493` @600f. |
| Rejeitar `dwSize` ponteiro/overflow | `src/brew/imedia.c` | Defesa se `LoadRes` ainda entregar tamanho inválido. |

PCM/MP3/mixer **não** alterados além do clamp. Pac-Mania/DD regressão: `audio_nonzero` intacto @600f.

## Resultados 600f serial (core pós-fix)

| Jogo | Status | `audio_nonzero` | Nota |
|---|---|---:|---|
| Family Pack (`real` / alias) | VARIED_VIDEO | **493903** | 60f curto: boot ok, mídia só após ~frame 120+input. Falso negativo de janela. |
| Ridge Racer | SOLID_FAIL (vídeo) | **881493** | Áudio OK (MP3 BAR). |
| Sports peteca/queimada | TIMEOUT @600 / áudio @60 | **85260** @60f | PCM 118 B toca; 600f timeout = thread/`sinal HID`, não silêncio. |
| Alice | TIMEOUT | inconclusivo | 28 IMedia criados; **0** `Play`/`RegisterNotify`. Travado em `BOOT_SIGNAL_CALL` (thread guest). |
| Foot Camp | TIMEOUT | inconclusivo | 61 IMedia; 2 `Play` no log; thread/`sinal HID` igual Alice. |
| Pac-Mania / Double Dragon | OK | 879060 / 882000 | Sem regressão. |

## Family Pack — por que regressão curta perde

1. `validation-transversal/family.log`: `CreateInstance` retorna, `EVT_APP_START`, input frame≥120, depois `music.mp3` → `audio_nonzero=492433`.
2. Regressão 60f (`honest` 060942): boot completa (`VARIED_VIDEO`), mas **antes** do menu/áudio → `audio_nonzero=0`.
3. Doc antigo `FAMILYPACK_REGRESSION.md` descrevia CreateInstance travado; build atual já passa CreateInstance. Causa do “silêncio” curto = **janela**, não codec.

## Não corrigido (sem evidência de bug de áudio)

- Alice/Foot Camp: ausência de `IMedia_Play` enquanto guest fica em `BOOT_SIGNAL_CALL` / thread — boot/HID, fora do pipeline PCM/MP3.
- Sports 600f TIMEOUT: mesmo padrão de thread; áudio já comprovado em 60f.

## 600f serial — DLL `lozeebo_v600_snap.dll` (build-smoke 2026-11-06 09:12, slice 50k + pump pós-yield)

Smoke: `libretro_smoke_snap.exe` + snap DLL; **sem** `ZEEMU_STUB_THREAD`. Logs: `*_v600b.log`.

| Jogo | EXIT | video | `audio_nonzero` | CreateDevice | Play | vs baseline |
|---|---:|---|---:|---:|---:|---|
| Alice | flaky (−1/1 @~60–180f; sem SUMMARY) | solid black (≤3 VIDEO) | — | **0** | **0** | ainda pré-CreateDevice; `alice_g` 600f ok mas CD=0/Play=0/`audio=0` |
| Foot Camp | 0 | varied (`xor=00004000`) | **0** | **0** | **2** | = `foot_g`; Play existe, áudio 0 |
| Family Pack | 0 | varied | **244921** | 1 | **4** | regressão vs `fp_hid600b` **493903** / Play **674** |
| Action Hero | 0 | varied | **881358** | 1 | **20** | **OK** = `ah_hid600b` |

### FP áudio ~½ — causa + fix (2026-11-06)

- Baseline `fp_hid600b`: após 1º `MP3 stream=0`, ~671× `IMedia_Play … voice=0` (re-Play via PCM sujo).
- Regressão `fp_v600b`: 1º `MP3 stream=0`, depois **1×** `MP3 stream=0 (ja ativo)` — sem restart; mono MP3 gravava só L no buffer stereo → ~½.
- **Fix**: `zmp3_restart` no re-Play (`imedia.c`); `zmp3_fill_voice` upmix mono→L/R (`mp3.c`). Sem PCM sujo.
- Pós-fix 600f: `audio_nonzero=489842` (≈ baseline 493903), `MP3 stream=0` + `(restart)`, video presente.

### Alice HID CreateDevice (2026-11-06 ~09:43)

**Fix** (`src/brew/boot.c`): `IHID_GetNextConnectEvent` (slot 5) entrega **1** evento sintético só se `g_hid_connect_event_avail && !g_hid_connect_signal`:
- `*phDevice=ZHID_HANDLE`, `*ppdevInfo=0`, `*pnStatus=1`, `R0=0`
- Alice `.mod` @`0x599F4` exige `*ppdevInfo==0` pra chamar CreateDevice
- FP/AH/DD usam `RegisterForConnectEvents` → `g_hid_connect_signal!=0` → fila vazia `R0=5` (sem sintético)
- Reset `g_hid_connect_event_avail=true` em `zboot_start`

**Evidência** (smoke isolado, DLL `74C86E1C…`, **sem** `ZEEMU_STUB_THREAD`, dump=`nul`):
| Run | EXIT | CreateDevice | Play | NOTES |
|-----|------|--------------|------|-------|
| alice600k | -1 (~9s, sem SUMMARY) | **1** + synthetic | 0 | pós-CD: IMedia_Set×28, TTDMM ASSERT@269, hist PC loop `0xBB***`/`0x5F***`; sem linha `CRASH HOST` |
| alice600j (`ZEEMU_STUB_THREAD=1`) | 0 + SUMMARY | **0** | 0 | stub mata thread guest → nunca poll HID |
| FP60 (stub thread) | (sem SUMMARY neste lote) | 1 via Register | — | sem synthetic |

**Smoke caveats (falsos EXIT=-1):**
1. **PPM dump** (`argv[3]` real path): fwrite 640×480×3 cada frame → host -1 ~frame 120–180. Use `nul` ou omit dump.
2. **`ZEEMU_STUB_THREAD=1`**: Alice/Foot nunca chegam em GetConnected/CreateDevice. FP/AH ok (Register path).
3. cwd deve ser repo root (assets/bars); smoke copy só DLL+exe.
4. Orphans `libretro_smoke` + rebuild mid-run → DLL lock / log contaminado.

**Validação 2026-11-06 ~10:10** (`lozeebo_alice_hid.dll` = x64/Release pós-fill 28B):
| Run | CreateDevice | Play | audio_nonzero | NOTES |
|-----|--------------|------|---------------|-------|
| alice600n | **1** + synthetic | 0 | — | VIDEO×10; Set×28; TTDMM@269 + softfloat `0xBB***` — **pós-HID** |
| fp600 | 1 via Register (**sem** synthetic) | 2 (MP3 + restart) | **489842** | FP áudio ~489k OK |
| ah120 | 1 via Register | 16+ | 175802 @120f | AH Play OK |

**Fill Position/Axes** (`input.c/h`): `ZINPUT_HID_POS_WORDS` 25→**7** (RelativeAxes…Rz = 28B). Alice passa temps de stack com gap `0x1C`; zeroar 100B smash.

**Ainda aberto (fora do mínimo HID ABI):**
- Alice: `IMedia_Play=0` pós-CreateDevice; TTDMM/softfloat no load de assets (não SEH).
- HIDDevice slots 10–13 handlers ok; log `STUB:` é pré-dispatch (cap 256).

### Próximo (prioridade)

1. Alice pós-CreateDevice: TTDMM@269 / softfloat loop → `IMedia_Play` — **HID ABI mínimo fechado**.
