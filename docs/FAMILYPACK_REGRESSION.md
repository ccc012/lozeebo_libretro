# investigacao: regressao do Zeebo Family Pack (real_family_pack_game.mod)

**Projeto:** zeebo_libretro  
**Modulo:** tests/roms/real_family_pack_game.mod (CLSID 0x010903C6 — "Zeebo Family Pack")  
**Data:** 05/11/2026  
**Autor:** subagente OpenCode  
**Status:** investigacao concluida (video ok, audio ainda quebrado)

---

## 1. Resumo executivo

O jogo *Zeebo Family Pack* era reportado como tendo **video e audio funcionando** em versoes anteriores, mas apresentou uma **regressao**.

Execucao do smoke test solicitado:

```
$env:ZEEMU_STUB_THREAD=1
.\build-smoke\Release\libretro_smoke.exe .\build-smoke\Release\lozeebo_libretro.dll .\tests\roms\real_family_pack_game.mod .\fp_test.ppm 300
```

Resultado obtido (log `fp_smoke_final.log`):

```
[SMOKE-SUMMARY] video_frames=300 video_first=00000E94 video_xor=0002D1E1 video_varied=1 audio_frames=220500
audio_nonzero=0 audio_peak=0
```

| Metrica | Valor obtido | Esperado | Status |
|---|---|---|---|
| `video_xor` | `0002D1E1` | != 0 | **OK** (video nao esta em tela preta) |
| `video_varied` | 1 | 1 | **OK** (framebuffer varia) |
| `audio_nonzero` | 0 | > 0 | **FALHA** (audio silencioso) |
| `audio_peak` | 0 | > 0 | **FALHA** |

Conclusao: **o video ja esta funcionando** (nao e mais tela preta `video_xor=00000000`). O problema restante e **audio silencioso** (`audio_nonzero=0`).

---

## 2. O que o video ja estava consertado

Conforme documentado em `BLOCKERS_ANALYSIS.md` (sec. 2.A, item `glDrawArrays`):

> Antes: chamava `draw_test_quad()` (hardcoded, branco)  
> Agora: chama `draw_prim(...)` com dados reais  
> Impacto: Family Pack agora renderiza cores

Isso confirma que a regressao de **video** (tela preta / `video_xor=00000000` citada na tarefa) ja foi resolvida no codigo atual. O framebuffer varia (`video_varied=1`) e o primeiro frame nao e preto.

---

## 3. Causa raiz do audio silencioso

### 3.1 O boot nao completa

O log de boot mostra que `IModule_CreateInstance(clsid=0x010903C6)` e invocado (via `guest_call` em `0x00000224`) mas **nao retorna** dentro dos 300 frames do smoke test. O estado de boot fica travado em `BOOT_CREATE_APPLET`:

```
zboot_process_timers: state=CreateInstance, skipping timer processing   (303 vezes)
```

Nunca aparece:
- `boot: CreateInstance retornou ...`
- `boot: applet dump ...`
- `boot: EVT_APP_START tratado`
- estado `APP_START` / `RUNNING`

Como `BOOT_APP_START` nunca e alcancado, o applet nunca entra em `BOOT_RUNNING`, o loop principal do jogo nunca roda e **nenhum objeto de media (IMedia) e criado nem toca**. Resultado: nenhuma fonte de audio ativa, `zaudio_render`/`zmp3_render_all` produzem silencio, `audio_nonzero=0`.

### 3.2 Por que o CreateInstance trava (disassembly)

O rastreio de PCs (ultimos 64) mostra um **loop apertado** no applet:

```
0x00000E94  0x00000E98  0x00000E9C   (ciclicamente)
```

Desmontando o trecho do mod (`real_family_pack_game.mod`, carregado a partir do endereco 0):

```
0xE90: bl   0x72FB8        ; chama funcao de poll
0xE94: cmp  r0, r5        ; compara retorno com contador
0xE98: movne r5, r0       ; se diferente, r5 = r0
0xE9C: beq  0xD98         ; se igual, retorna do CreateInstance (epilogo em 0xD98)
0xEA0: mov  r0, #0x48
0xEA4: bl   ...           ; continua inicializacao
...
0xD98: sub  r11, r11, #0x1C   ; epilogo
0xD9C: ldm  sp, {...}       ; pop registradores
0xDA0: bx   lr              ; RETORNA do CreateInstance
```

A funcao `0x72FB8` e um poll de hardware/status:

```
0x72FB8: subs r5, r5, #1      ; decrementa contador do chamador
0x72FBC: bcc  timeout         ; se estourou, vai para timeout
0x72FC0: ldr  r4, [r11, #0x28] ; pega ponteiro de buffer
0x72FC4: ldr  r1, [r4], #1    ; le byte *r4++ (ponteiro avanca)
0x72FC8: mov  r0, r6          ; retorno = r6 (do chamador)
0x72FCC: str  r4, [r11, #0x28] ; salva ponteiro atualizado
...
0x73010: mov  r0, r7
0x7301C: bx   lr
```

O loop do chamador (`0xE94`) espera ate que o retorno de `0x72FB8` (`r0`) estabilize em relacao ao contador `r5`; somente ai o `CreateInstance` retorna pelo epilogo em `0xD98`.

Como `r0 = r6` e `r6` e um registrador do chamador que **nao muda dentro do loop**, a condicao `r0 == r5` nunca se estabiliza dentro de 300 frames (~300M de instrucoes). O applet fica em **loop infinito de espera** e o CreateInstance nunca retorna.

### 3.3 Consequencia para o audio

O caminho de audio de MP3 esta implementado e funcional (`imedia.c` `IMedia_Play` -> `zmp3_play` -> `zmp3_render_all`, chamado em `libretro_core.c` `retro_run`). Porem ele so e acionado se o jogo criar um objeto `IMedia` (classe `MEDIAMP3_REAL`) e chamar `Play`. Como o boot nunca completa, **nenhum objeto de media e criado** e nenhum fluxo MP3 e iniciado. Logo `audio_nonzero=0`.

---

## 4. Investigacao das suspeitas listadas na tarefa

Foram verificadas todas as areas apontadas como possiveis causadoras da regressao:

### 4.1 Injecao applet+0x6C (0x2F440) — RE4

**Codigo (src/brew/boot.c):**
- Ponto de escrita (GRAPHICS Init, linha ~2320): `if (g_applet_obj && g_applet_clsid == 0x0108AF6Cu)` -> so RE4 (0x0108AF6C).
- Ponto de leitura/chamada (timer, linha ~1366): `if (g_applet_clsid == 0x0108AF6Cu || g_applet_clsid == 0x0108FF1Au)` -> RE4 **ou Zeeboids** (0x0108FF1A).

**Conclusao:** A injecao ja e **condicional apenas para RE4/Zeeboids**. O Family Pack (CLSID 0x010903C6) **nao e injetado** e **nao eh afetado**. O log do Family Pack nao contem nenhuma linha `RE4 applet+0x6C` nem `GRAPHICS_Init: RE4 applet+0x6C`. Nao ha regressao aqui para o Family Pack.

Nota: ha uma pequena inconsistencia entre os dois pontos (escrita so para RE4; chamada para RE4+Zeeboids), mas isso nao impacta o Family Pack.

### 4.2 GetHandler (ISHELL slot 32)

**Codigo (src/brew/boot.c, linhas ~1668-1680):**
```c
case 32: /* GetHandler */
    if (r1 == AEECLSID_MEDIA_REAL || r1 == 0x01005500u) {
        uint32_t handler_clsid = AEECLSID_MEDIAPCM_REAL; /* 0x01005511 (PCM) */
        ...
        g_cpu.r[0] = 0;
    } else {
        g_cpu.r[0] = 1;
    }
```

**Conclusao:** `GetHandler(MEDIA_REAL)` retorna `MEDIAPCM_REAL` (PCM), nao `MEDIAMP3_REAL`. Isso e proposital para jogos PCM/ADPCM (ex: Double Dragon, conforme comentario no codigo). O Family Pack **nao invoca `GetHandler`** durante o boot (nenhuma linha `IShell_GetHandler` no log), portanto isso nao e a causa do silencio do Family Pack.

### 4.3 MEDIA_REAL create_instance

**Codigo (src/brew/boot.c, `real_create_instance`, linha ~1457):**
```c
if (clsid == AEECLSID.MEDIA_REAL || clsid == 0x01005500u) {
    return zbrew_create_media_util(); /* stub IMedia generico */
}
```

**Conclusao:** Retorna um stub generico. O Family Pack **nao cria nenhum objeto de media** durante o boot (os unicos CLSIDs criados sao: DISPLAY 0x01001001, FILEMGR 0x01001003, 0x01001056, HID 0x0106C411, SIGNAL 0x01041207, EGL 0x01014BC4, GL 0x01014BC3). Nenhum `MEDIAMP3_REAL`/`MEDIA_REAL` e instanciado.

### 4.4 Signal implementation / HID changes

O Family Pack **registra** `IHID_RegisterForConnectEvents` (signal=0x10012488) durante o `CreateInstance`, definindo `g_hid_connect_pending=true`.

Porem, em `src/brew/boot.c` `zboot_process_timers` (linhas ~1198-1201):

```c
if (g_state != BOOT_RUNNING) {
    LOGI("...skipping timer processing");
    return;   // <- sai ANTES de processar o sinal de conectacao HID
}
```

O sinal de conectacao HID so e disparado quando `g_state == BOOT_RUNNING` (linhas ~1204-1217), ou seja, **depois** de o boot completar. Como o `CreateInstance` do Family Pack trava antes disso, o sinal nunca e entregue durante a inicializacao.

**Observacao importante:** mesmo que esse sinal fosse entregue durante o `CreateInstance`, a arquitetura atual **nao suporta chamadas guest aninhadas** dentro do `CreateInstance`: `guest_call` define `LR = ZT_GUEST_RETURN` e, ao retornar o callback, `zboot_on_guest_return` (estado `BOOT_CREATE_APPLET`) leria `g_applet_out == 0` (nao preenchido ainda) e falharia o boot (`BOOT_FAILED`). Entao entregar o sinal de callback durante o `CreateInstance` quebraria o boot em vez de desbloquear-lo.

### 4.5 IMedia vtable order rewrite / mixer / pcm

O caminho de saida de audio esta correto (`libretro_core.c` `retro_run` chama `zaudio_render` + `zmidi_render` + `zmp3_render_all` e depois `audio_batch_cb`). O problema nao e a saida, e sim a **ausencia de fontes de audio** (nenhum objeto IMedia/fluxo MP3 criado porque o boot nao completa). As mudancas recentes em `mixer.c`/`pcm.c` (canais, pause, mute, pan, ADPCM) nao silenciam o audio por si so; simplesmente nao ha vozes ativas no Family Pack.

---

## 5. Conclusao

| Item | Status |
|---|---|
| Regressao de video (tela preta / `video_xor=00000000`) | **Ja resolvida** — video funciona (`video_xor=0002D1E1`, `video_varied=1`) |
| `applet+0x6C` (0x2F440) injetado para jogos nao-RE4 | **Nao** — ja e condicional apenas para RE4/Zeeboids |
| `GetHandler` retornando PCM em vez de MP3 | Nao se aplica ao Family Pack (nao chama `GetHandler` no boot) |
| `MEDIA_REAL create_instance` stub | Nao se aplica (Family Pack nao cria media no boot) |
| Audio silencioso (`audio_nonzero=0`) | **Confirmado** — causado pelo travamento do `CreateInstance` |

**Causa raiz do audio:** o `IModule_CreateInstance` do Family Pack entra em um loop de espera (`0xE94-0xE9C`, polling da funcao `0x72FB8` até o retorno estabilizar) que nunca se resolve dentro da janela do smoke test. Com isso o boot nunca atinge `BOOT_RUNNING`, o loop principal do jogo nao roda, nenhum objeto `IMedia`/fluxo MP3 e criado, e o audio fica em silencio.

### Próximos passos recomendados

Para restaurar o audio (`audio_nonzero > 0`) e necessario desbloquear o `CreateInstance` do Family Pack. Isso provavelmente exige:

1. Identificar exatamente o que a funcao `0x72FB8` esta aguardando (qual registrador/memoria de hardware/status deve estabilizar) e garantir que o emulador atualize esse valor durante o `CreateInstance`.
2. Ou, alternativamente, rever a politica de `zboot_process_timers` que salta o processamento de timers/sinais enquanto `g_state != BOOT_RUNNING` — observacao: qualquer entrega de callback de guest durante o `CreateInstance` exigiria suporte a chamadas guest aninhadas, que a arquitetura atual **nao suporta** (levaria a `BOOT_FAILED` por `g_applet_out == 0`).

Nenhuma dessas alteracoes e uma correcao de uma linha; requerem analise mais profunda do comportamento do applet do Family Pack.

---

## 6. Evidencias

- Comando executado: `.\build-smoke\Release\libretro_smoke.exe .\build-smoke\Release\lozeebo_libretro.dll .\tests\roms\real_family_pack_game.mod .\fp_test.ppm 300` (com `$env:ZEEMU_STUB_THREAD=1`)
- Log: `fp_smoke_final.log` (linhas 1565-1566 com o `SMOKE-SUMMARY`)
- Frame gerado: `fp_test.ppm`
- Fontes inspecionadas: `src/brew/boot.c` (boot state machine, `real_create_instance`, `GetHandler`, injecao `applet+0x6C`, `zboot_process_timers`), `src/brew/imedia.c` (`IMedia_Play`, `zmp3_play`), `src/audio/mp3.c` (`zmp3_render_all`), `src/core/libretro_core.c` (`retro_run` pipeline de audio), `src/brew/brew.c` (`trap_dispatch`, `ZT_GUEST_RETURN`), `src/cpu/cpu.c` (guest call / stack overflow)
- Documentacao relacionada: `BLOCKERS_ANALYSIS.md` (video do Family Pack ja consertado), `ANALYSIS_FINDINGS.md` (loop de evento nao e o bloqueador)
