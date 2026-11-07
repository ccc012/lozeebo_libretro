# Double Dragon — Input Validado (2026-11-07)

**Branch:** `front/dd-playable`  
**HEAD:** `703e0c7` (frameskip option)  
**Worktree:** `C:\Users\Lucas\AppData\Local\Temp\opencode\zeebo-worktrees\dd-playable`

## Objetivo

Investigar relato "personagem NÃO responde controle" em Double Dragon.

## Resultado

**Input funciona corretamente.** Gate `-Playable` PASS com prova de movimento e soco.

## Validação

```
.\tools\dd-gate\run.ps1 -Playable -BuildDir build-dd
```

**Resultado:**
- `PASS frames=1400 exit=0`
- `dump=47 unique=41 LEFT_distinct=4 RIGHT_distinct=3`
- `DrawArrays=20`
- `audio_nonzero=684044`
- `elapsed_ms=10797` (~130 FPS)

**Evidência:**
- `artifacts/dd-gate/dd-playable.log`
- `artifacts/dd-gate/dd-playable.ppm` SHA256 `6589C89C...`
- `artifacts/dd-gate/dump-playable/*.ppm` (47 frames temporais)

## Diagnóstico Caminho Input

1. **RetroArch/smoke → libretro callbacks:** OK
   - `retro_set_input_poll` / `retro_set_input_state` registrados
   - `smoke.c:input_state()` linha 218: implementa ZEEMU_SMOKE_SEQ
   
2. **libretro_core → zinput:** OK
   - `poll_input()` linha 542: chama `input_poll_cb()` + `input_state_cb()`
   - `zinput_update()` atualiza `g_keys[0]` / `g_prev_keys[0]`
   
3. **zinput → boot HID:** OK
   - `zinput_pressed()` = `g_keys[0] & ~g_prev_keys[0]`
   - `zboot_process_timers()` linha 1654: lê pressed/released
   - `hid_evt_push()` enfileira eventos
   
4. **boot HID → guest:** OK
   - `g_hid_button_signal` disparado linha 1710
   - `zboot_signal_fire()` chama callback guest
   - Log: `[INFO] [LOZeebo] HID: uid=0x0106C40A down=1 q=1`
   - Log: `[INFO] [LOZeebo] boot: sinal (hid-button) callback=0x0001BDF4`
   
5. **Guest → movimento visível:** OK
   - LEFT hold 90f: 4 PPM distintos
   - RIGHT hold 90f: 3 PPM distintos
   - A/B punches: glDrawArrays count=20

## Interpretação Relato Original

Usuário esperava resposta imediata em "CARREGANDO..." (frames 0-400). Jogo permanece em loading screen loop até START avançar para stage (frame ~420). Após stage, LEFT/RIGHT/A/B funcionam e alteram visual.

Relato "NÃO responde" = expectativa input funcionar em loading vs realidade = input só processa pós-stage.

## Arquitetura Input Confirmada

- RetroPad digital/analog → `zinput_update()` por frame
- Buttons (A/B/START) → HID button events + signal callback
- D-pad → HID position axis (0x8000 centro, 0x0000 min, 0xFFFF max)
- DD remapping: START UID `0x0106C402` → `0x0106C403` (HOME/Back id=8)
- Analog/D-pad espelhados: Family Pack navega por position, DD por buttons

## Conclusão

Input layer completo e validado. Gate `-Playable` comprova movimento LEFT/RIGHT e soco A/B com PPM distintos. Sem bugs. Sem necessidade de alteração.

Relato original = expectativa incorreta de interatividade durante loading screen.
