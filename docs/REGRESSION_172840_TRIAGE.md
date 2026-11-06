# Triagem regression-honest 20261105-172840

Fonte: `tests/regression-honest/20261105-172840/`  
Core: `dist\lozeebo_libretro.dll` · 60 frames · timeout 45s · ZEEMU_STUB_THREAD removida  
Totais reportados: SOLID_FAIL 58 · TIMEOUT 2 · VARIED_VIDEO 9  
**Não inventar sucesso:** áudio NZ=0 em todos; nenhum título passou critério "jogo jogável".

Critério deste doc: VARIED_VIDEO = `video_varied=1` no smoke. **Real** = desenho do guest (IDisplay/GL com conteúdo reconhecível do título). **Residual** = artefato de stub/teste/boot (padrão geométrico genérico, loading preso, framebuffer quase sólido com 1–2 pixels de ruído, ROM de diagnóstico).

---

## 9 × VARIED_VIDEO

| # | ROM | frame_sha256 (12) | Evidência | Veredito |
|---|-----|-------------------|-----------|----------|
| 11 | `ddragonz.mod` | `5b59b172…` | 16× `IDisplay_DrawRect` + 16× `DrawText "CARREGANDO…"`; PPM ~preto com ~73 cores residuais de glifo; stuck no loading | **REAL parcial** (loading screen do guest; não é menu/jogo) |
| 13 | `double_dragon.mod` | `5b59b172…` | Idêntico ao #11 (mesmo SHA, mesmos DrawRect/DrawText) | **REAL parcial** (mesmo cluster Double Dragon) |
| 22 | `pac-mania.mod` | `bb555aba…` | `glClear` + `glDrawElements`; PPM preto + faixa `0x202020` + azul `1E1EC8` (~3 cores) | **REAL parcial** (GL guest; cena mínima, não título completo) |
| 31 | `real_ddragon_game.mod` | `5b59b172…` | Clone do #11/#13 | **REAL parcial** |
| 32 | `real_family_pack_game.mod` | `1e9394fa…` | `CreateInstance` retorna; `glClear` + `glDrawArrays`; PPM quase branco (`FFFFFF` dominante, ~10 tons) | **REAL parcial** (boot GL; UI/jogo incompletos; áudio 0 — ver `FAMILYPACK_REGRESSION.md`) |
| 33 | `real_pacmania_game.mod` | `bb555aba…` | Clone do #22 | **REAL parcial** |
| 43 | `test_draw.mod` | `fff4bcb8…` | ROM 76 bytes, CLSID desconhecido; PC preso em `0x3C`; PPM azul/vermelho (`FF0000`/`0000FF`); sem IDisplay/GL | **RESIDUAL** (ROM de diagnóstico / framebuffer de teste; não é jogo) |
| 49 | `um_jogo_de_ovos.mod` | `baa62248…` | `glClear` + `glDrawElements`/`glDrawArrays`; `video_xor=0` mas `varied=1`; PPM ~preto com ~10 px brancos | **RESIDUAL fraco / borderline** (pipeline GL tocado; conteúdo visual desprezível) |
| 61 | `zeebo_family_pack.mod` | `1e9394fa…` | Clone do #32 | **REAL parcial** |

### Resumo VARIED

| Classe | Contagem | Itens |
|--------|----------|-------|
| REAL parcial (guest desenhou algo do título) | 7 | ddragon×3, pacmania×2, familypack×2 |
| RESIDUAL | 1 | test_draw |
| RESIDUAL/borderline | 1 | um_jogo_de_ovos |

**Nenhum** dos 9 é "vídeo de jogo completo". Os 7 "reais" são loading/boot GL. `test_draw` e `ovos` não contam como sucesso de título.

Clusters de SHA (cópias / builds irmãos):

- `5b59b172` — ddragonz / double_dragon / real_ddragon_game  
- `bb555aba` — pac-mania / real_pacmania_game  
- `1e9394fa` — real_family_pack_game / zeebo_family_pack  

---

## 2 × TIMEOUT

### 1 · `action_hero_3d_-_wild_dog_&_imicro3d.mod` (45,465 s)

- Boot fica em `IModule_CreateInstance` (nunca loga `CreateInstance retornou`).
- `a3dres.res` / `string.txt` ausentes no VFS.
- Stub `0x01002001` método 38; depois `CreateInstance case 5` com CLSID `0` / `0x0100100C`.
- Guest passa a tratar **opcodes/dados como CLSID** (`stub: clsid=0xD010072D`, `0xE18CC332`, `0xE1A01007`…) — vtable/objeto corrompido.
- `SetupNativeImage(0x01004001)` com `pBuffer=0` → falha; em seguida `malloc(120442925)` (~115 MiB) falha em loop ao reabrir `a3dres.res`.
- Sem PPM. Diagnóstico: **CreateInstance não retorna**; recurso 3D ausente + stub/vtable lixo + alloc absurdo.

### 14 · `fifa_09.mod` (45,51 s)

- `CreateInstance` e `EVT_APP_START` OK; 1× `IDisplay_DrawRect` amarelo (clear).
- Lê vários `udata/*.f08`.
- Trava em loop guest em `0x9AA7C…9AA8C`:
  - `LDR r2,[r4,#4]; STR r1,[r2,r0,LSL#2]; ADD r0,#1; CMP r0,#0x8000; BLT`
  - clear de **32768 words** (128 KiB) em software; R0≈0x258A no timeout.
- `WATCH devzone` / `devvt ZERO` no meio do clear; só 2–3 `retro_run` (frame_count estático bugado no log).
- PPM existe, SHA `0e2476ee…` (= tela branca/sólida compartilhada com vários SOLID_FAIL).
- Diagnóstico: **boot lento demais** — fill word-a-word estoura 45s antes de completar 60 frames. Não é hang lógico infinito; é custo de emulação.

---

## Correção transversal aplicada

**Arquivo:** `src/cpu/cpu.c` — `try_hle_word_fill()` chamado de `zcpu_run`.

Detecta o padrão ARMCC acima (qualquer PC, não só FIFA), faz `memset`-equivalente via `zmem_host_ptr`, ajusta R0/R2/flags/PC. Cobre fills guest que bypassam o helper `memset` (0x004).

- Self-check: `tests/check_word_fill_hle.c` (decode imm12 + offset BLT).
- **Não verificado** nesta sessão contra rebuild+smoke FIFA (só análise de log + patch). Re-rodar `fifa_09` após rebuild para confirmar saída do TIMEOUT.
- Action Hero **não** coberto por este patch (causa distinta: CreateInstance/recursos/vtable).

---

## O que NÃO foi feito / não afirmar

- Não reexecutou a suíte 69 ROMs após o patch.
- Não corrigiu Action Hero (`a3dres.res`, CLSID lixo, malloc monstro).
- Não promoveu nenhum VARIED a "PASS jogável".
- `video_varied=1` sozinho continua métrica fraca (`ovos`, `test_draw`).
