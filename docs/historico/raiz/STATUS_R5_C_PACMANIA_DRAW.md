# STATUS R5-C — Pac-Mania: caminho de desenho

## Resumo (8 linhas)

- **Fix aplicado e testado**: `AEEHelper_strtowstr`/`wstrtostr` (slots `0x040`/`0x044` da
  tabela `AEEHelperFuncs`) estavam implementados em `helpers.c` mas nunca conectados ao
  dispatch — todo `STRTOWSTR` do Pac-Mania (7x por ciclo de UI) caía em "não implementado".
  Corrigido em `src/brew/helpers.c`. Log confirma: zero warnings restantes.
- **Causa raiz do bloqueio real, com evidência de código-fonte, não suposição**: o slot 4
  do `IDisplay` (`DrawText`, layout de 48 slots documentado no próprio `boot.c:264`) cai
  no `case 4` do dispatch genérico de `zbrew_handle_stub()`, que só trata
  `AEECLSID_HID_REAL`/`ZCLSID_HID_DEVICE` e devolve sucesso silencioso (`r0=0`, sem
  desenhar nada, sem log) para qualquer outro CLSID — inclusive `AEECLSID_DISPLAY_REAL`.
  **Isso está fora do meu escopo (é case de IDisplay = Tarefa B)** — devolvido como
  diagnóstico pronto pra implementar.
- Tela continua preta mesmo depois do fix de string: o fix era necessário mas não
  suficiente — o consumidor da string convertida (`DrawText`) está mudo.
- `BitBlt` (slot 6) não aparece nem uma vez no log desta janela de teste — o jogo não
  chega a tentar usá-lo ainda (só teria log se caísse no `default`, que WARN até 32x).
- Regressão checada nos 4 jogos Tier 1: sem mudança de comportamento em Family Pack,
  Zeeboids, Double Dragon. Nenhum descarrilamento novo.

---

## Investigação (antes de codar)

Baseline: `tests/libretro_smoke.exe` contra `tests/roms/real_pacmania_game.mod`, sem
nenhuma mudança de código.

```
frame 1:   instrucoes=4214   PC=0xF0000A40 halted=1 fb[0]=0xFF000000
frame 61:  instrucoes=75306  PC=0xF0000A40 halted=1 fb[0]=0xFF000000
frame 121: instrucoes=145686 PC=0xF0000A40 halted=1 fb[0]=0xFF000000
```

`PC=0xF0000A40` com `halted=1` é o retorno normal de guest call
(`ZTRAP_ID(0xF0000A40)=0x290=ZT_GUEST_RETURN`, confirmado em rodadas anteriores — não é
travamento, é snapshot do estado no momento do log).

Filtrando o log completo (sem o spam de `IShell_SetTimer`), o padrão que aparece é:

```
[INFO] IDisplay_DrawRect: rect=0,0 640x480 frame=0xFFFFFFFF fill=0x00000000 flags=0x2   <- clear pra preto
[WARN] AEEHelper_strtowstr (slot 0x040) nao implementado (R0=... R1=... LR=...)          <- x7
[WARN] AEEHelper_strtowstr (slot 0x040) nao implementado (...)
... (7 no total, repetindo em ciclos de 2 padroes de R1/LR alternados)
```

16 ciclos de `DrawRect(preto)` seguidos de 7 chamadas de `strtowstr` cada, sem NENHUM
outro tipo de desenho (nada de `DrawText`, `BitBlt`) em nenhum lugar do log.

### Rastreamento do `strtowstr`

`src/brew/helpers.c` já tinha `guest_strtowstr()`/`guest_wstrtostr()` implementadas
(conversão ASCII↔UTF-16 correta), mas **nunca conectadas** ao `switch (slot * 4)` de
`zbrew_handle_helper()` — a tabela de nomes em `aee_ids.h:87` documenta os slots
`0x040/0x044/0x048/0x04C` (`strtowstr/wstrtostr/wstrtofloat/floattowstr`), mas o switch
pulava direto de `0x03C` (`wsprintf`) pra `0x058` (`wstrlower`).

Os valores de `R0` nos warnings (`0x00015440`, `0x000157A8`, `0x000157A0`, etc.) caem
dentro da área de dados estáticos do módulo (código vai até `~0x23BD0`), consistente com
strings constantes de UI sendo convertidas — bate com a assinatura real do BREW
`STRTOWSTR(const char* cs, AECHAR* wcs, int wcsBufferSize)` (**fonte primeiro**, não
destino) — por isso a chamada no dispatch usa `guest_strtowstr(r1, r0, r2)` (dst=r1,
src=r0), invertendo a ordem dos parâmetros da função helper interna (que foi escrita com
`dst` primeiro).

## Fix aplicado

`src/brew/helpers.c`, dentro de `zbrew_handle_helper()`:

```c
case 0x040: /* STRTOWSTR(cs, wcs, wcsBufferSize) - AEEStdLib: src primeiro */
    guest_strtowstr(r1, r0, r2);
    break;
case 0x044: /* WSTRTOSTR(ws, s, sizeInBytes) - AEEStdLib: src primeiro */
    guest_wstrtostr(r1, r0, r2);
    break;
```

Não implementei `0x048`/`0x04C` (`wstrtofloat`/`floattowstr`) — não apareceram nenhuma
vez no log do Pac-Mania, e eu não tinha uma referência confiável de assinatura pra evitar
"consertar" às cegas (regra da rodada: só editar com hipótese + evidência).

### Log depois do fix

```
$ grep -i warn <log> | grep -v SetTimer
(vazio - zero warnings)

frame 1:   instrucoes=4214   fb[0]=0xFF000000   (identico ao baseline)
frame 61:  instrucoes=75306  fb[0]=0xFF000000   (identico ao baseline)
frame 121: instrucoes=145686 fb[0]=0xFF000000   (identico ao baseline)
```

`instrucoes=` idêntico ao baseline em todos os 3 checkpoints — esperado, já que a HLE
trap consome sempre 1 instrução de guest independente do que a implementação faz por
trás. O fix removeu os warnings mas **não mudou nenhum comportamento visível** porque o
consumidor da string (abaixo) está mudo.

## Causa raiz do bloqueio real (fora do meu escopo, documentado pra Tarefa B)

`src/brew/boot.c`, comentário na linha 262-264:

```c
/* NAO rotear AEECLSID_DISPLAY_REAL para uma vtable propria com os
 * traps ZT_DISP_* legados: o layout real do IDisplay (AEEDisplay.h, ver
 * BrewDisplay do zeemu) tem 48 slots ([4]DrawText [6]BitBlt [7]Update
```

Ou seja: o próprio código já documenta que o slot 4 = `DrawText`. Mas dentro de
`zbrew_handle_stub()`, o `case 4:` (linha 964) só tem branches para
`AEECLSID_HID_REAL`/`ZCLSID_HID_DEVICE`:

```c
case 4:
    if (clsid == AEECLSID_HID_REAL) { /* IHID_GetDeviceInfo */
        ...
        break;
    }
    if (clsid == ZCLSID_HID_DEVICE) { /* IHIDDevice_GetDeviceInfo */
        ...
        break;
    }
    g_cpu.r[0] = 0;    // <-- QUALQUER outro clsid (inclusive AEECLSID_DISPLAY_REAL)
    break;             //     cai aqui: "sucesso", zero desenho, ZERO log
```

Isso é diferente (e mais perigoso) que cair no `default:` do switch — o `default`
(linha 1194) **loga um warning** (`stub: clsid=... metodo=...`) até 32 vezes; o `case 4`
compartilhado NÃO loga nada porque já tem um `break` explícito no fim. É por isso que
mesmo depois do fix de `strtowstr`, nenhum rastro de "API faltando" aparece no log — a
chamada de `DrawText` do Pac-Mania está sendo **engolida silenciosamente**.

Confirmei também que `case 6` (`BitBlt`) **não existe em nenhum lugar** dentro de
`zbrew_handle_stub()` — cairia no `default` (que warn), mas não apareceu nenhuma vez no
log desta janela de teste, ou seja, o jogo ainda não tentou chamar `BitBlt` (pode
acontecer mais adiante no jogo, ou pode não ser o caminho de desenho dele).

### Recomendação concreta pra Tarefa B

1. Adicionar um `case 4:` (ou o slot equivalente correto no layout de 48) dentro de
   `zbrew_handle_stub()` tratando `clsid == AEECLSID_DISPLAY_REAL` como `DrawText`,
   ANTES do fallback genérico de HID — hoje o fallback intercepta silenciosamente.
2. Pra descobrir a assinatura exata dos argumentos (texto, fonte, x, y, cor, clip), um
   `LOGD` temporário no `case 4` dumpando `r0-r3` + `stack[0..2]` quando `clsid ==
   AEECLSID_DISPLAY_REAL` resolve rápido — os `R0`/`R1` que reportei acima (baseline,
   nos warnings de `strtowstr`) já são pistas de onde estão as strings de UI na memória.
3. Depois de implementar, o Pac-Mania deve mostrar pelo menos texto de menu — não sei se
   isso é tudo que falta (pode haver sprites via `BitBlt` depois), mas é o próximo
   bloqueador confirmado na cadeia.

## Regressão (Tier 1 completo, com o fix de strtowstr aplicado)

```
=== Pac-Mania ===     f1: instr=4214   f61: instr=75306  f121: instr=145686  fb=0xFF000000 (igual baseline)
=== Family Pack ===   f1: instr=729568 f61: instr=729568 f121: instr=729568  fb=0xFF000000 (igual baseline, regressao conhecida de signals - Tarefa A/R5-A)
=== Double Dragon === f1: instr=9222   f61: instr=13695  f121: instr=17671   fb: 0xFF000000 -> 0xFFFFFFFF (igual baseline, preto->branco)
=== Zeeboids ===      f1: instr=544671 f61: instr=544678 f121: instr=544678  fb=0xFF000000 (igual baseline, idle esperado)
```

Nenhum "CPU descarrilou" em nenhum dos 4. Nenhuma mudança de comportamento além do
Pac-Mania (zero warnings de strtowstr).

## Arquivos tocados

- `src/brew/helpers.c` — cases `0x040`/`0x044` conectados (`STRTOWSTR`/`WSTRTOSTR`).
  Único arquivo com mudança de código nesta branch.
- Nenhuma mudança em `src/brew/boot.c` (cases de display são escopo da Tarefa B),
  `src/brew/boot.c`/signals (Tarefa A), nem `src/gpu/egl_gl.c` (Tarefa D).
