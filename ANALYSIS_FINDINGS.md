# ANALYSIS_FINDINGS

## CLAUDE #1 - Event loop / boot

- `retro_run()` chama `zbrew_tick(16)`, `zboot_tick(16)` e `zboot_process_timers()` antes de decidir se executa a CPU.
- O loop de evento não está quebrado nem ausente.
- `zboot_process_timers()` só volta a liberar a CPU quando não há timers ativos; se houver timer pendente, a CPU permanece em `halted`.
- Em `boot.c`, o ponto que realmente para a CPU é a transição de boot:
  - `BOOT_APP_START` define `g_cpu.halted = true`
  - `BOOT_TIMER_CALL` e `BOOT_SIGNAL_CALL` também deixam a CPU parada após o callback
- Para `Zeeboids`, o bloqueio atual observado no smoke test não parece ser falta de tick do loop, e sim descarrilamento do guest em `PC=0x04000000` depois de entrar em `AEEMod_Load`.

## Resposta direta

- `EVENT LOOP` é o bloqueador #1? **Não**.
- O loop chama o tick corretamente.
- O problema principal do `Zeeboids` hoje está antes disso: o applet entra no boot e depois descarrila.

## Fix sugerido

- Se a meta for evitar que timers mantenham a CPU parada para sempre quando o jogo espera callbacks, o ajuste mais seguro é permitir que o loop continue processando timer expirados e reavalie o estado antes de retornar de `retro_run()`.
- Código relacionado:
  - [`src/core/libretro_core.c`](C:/Users/Lucas/source/repos/zeebo_libretro/src/core/libretro_core.c)
  - linhas aproximadas `420-445`

