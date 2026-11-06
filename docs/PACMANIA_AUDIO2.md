# PAC-MANIA - Ausência de Áudio (Pós-Correção do GetHandler)

**Data:** 05/11/2026  
**Jogo:** Pac-Mania (CLSID: `0x01087B72`, arquivo: `tests/roms/real_pacmania_game.mod`)  
**Status:** Vídeo OK (`video_varied=1`), áudio **ainda silencioso** (`audio_nonzero=0`, `audio_peak=0`)  
**Comparação:** Double Dragon funciona com áudio PCM (`audio_nonzero=441000`, `audio_peak=1713`)  
**Arquivo de trace:** `pac_audio2_trace.log` (gerado com `ZEEMU_TRACE_HID=1 ZEEMU_TRACE_MEDIA=1`)

---

## 1. Smoke Test Executado

Comando executado:

```powershell
$env:ZEEMU_STUB_THREAD=1; $env:ZEEMU_TRACE_HID=1; $env:ZEEMU_TRACE_MEDIA=1; .\build-smoke\Release\libretro_smoke.exe .\build-smoke\Release\lozeebo_libretro.dll .\tests\roms\real_pacmania_game.mod .\pac_audio2.ppm 300
```

Resultado:

```
[SMOKE-SUMMARY] video_frames=300 video_first=FF000000 video_xor=0023D2E0 video_varied=1 audio_frames=220500 audio_nonzero=0 audio_peak=0
```

- **Vídeo:** 300 frames, variado — OK
- **Áudio:** 220500 frames gerados, todos zero (silêncio absoluto) — FALHA

Para comparação, Double Dragon:

```
[SMOKE-SUMMARY] ... audio_frames=220500 audio_nonzero=441000 audio_peak=1713
```

---

## 2. GetHandler Está Implementado, MAS Retorna o Valor Errado em R0

O `IShell_GetHandler` (slot 32) **foi implementado** em `src/brew/boot.c` (linhas 1668-1680). O trace confirma que ele é chamado e reconhece `MEDIA_REAL`:

```
Line 1295: [INFO] ISHELL: slot=32 (GetHandler) R0=0x10001270 R1=0x01005500 R2=0x00000000
Line 1296: [INFO] IShell_GetHandler: MEDIA_REAL -> MEDIAPCM_REAL (0x01005511)
```

Isso acontece 3 vezes no log (linhas ~1295, ~1357, ~1418). Sempre retorna `MEDIAPCM_REAL (0x01005511)`.

**PROBLEMA:** Na linha 1675 de `boot.c`, o handler faz:

```c
g_cpu.r[0] = 0; /* SUCCESS */
```

Ele coloca **0 (código de sucesso)** em R0, em vez de colocar o **CLSID (0x01005511)** em R0.

Segundo a referência Zeemu (`reference/zeemu-main/zeemu-main/brew/BrewShell.cpp`):

```cpp
cpu.set_reg(REG_R0, cls);  // retorna o CLSID de mídia específico em R0
```

A convenção BREW para `IShell_GetHandler` é que o **CLSID do handler seja o valor de retorno em R0**, não um código de sucesso.

Como consequência, o jogo lê R0 = 0 após o GetHandler e, em seguida, chama `CreateInstance` com CLSID = 0:

```
Line 1299: [INFO] ISHELL: slot=2 (CreateInstance) R0=0x10001270 R1=0x00000000 R2=0x2FFFFF28
Line 1300: [INFO] ISHELL CreateInstance calling real_create_instance(0x00000000)
Line 1303: [INFO] IShell_CreateInstance(0x00000000) -> 0x10005D60
Line 1323: [WARN] CreateInstance case 5: CLSID desconhecido 0x00000000 -> stub 0x10005DB0
```

Esse padrão se repete 3 vezes (linhas ~1299, ~1361, ~1422): **sempre `CreateInstance(0x00000000)`**, nunca `CreateInstance(0x01005511)`.

---

## 3. Nenhuma Criação de IMedia (0x01005511) Após o GetHandler

Busca por `MEDIAPCM`, `0x01005511`, `zbrew_create_media` no trace do Pac-Mania: **zero matches**.

O jogo nunca chama `CreateInstance(0x01005511)` após o GetHandler. Como GetHandler devolve 0 em R0, o jogo chama `CreateInstance(0)`, que retorna um **stub genérico** (via `make_stub_interface`), não uma interface `IMedia` funcional.

| CLSID chamado | Resultado | Interface criada |
|---------------|-----------|------------------|
| `0x00000000` (após GetHandler quebrado) | stub genérico | Sem funcionalidade de áudio |
| `0x01005511` (MEDIAPCM_REAL, o que DD chama) | `real_create_instance` → `zbrew_create_media_pcm()` | IMedia funcional com Play/RegisterNotify/SetMediaData |

---

## 4. IMedia_Play Nunca É Chamado

Busca por `IMedia_Play`, `IMedia_RegisterNotify`, `SetMediaData`, `SetMediaParm`, `IMedia_notify`, `zbrew_create_media`, `voice=` no `pac_audio2_trace.log`: **zero matches**.

Nenhuma chamada à interface `IMedia` ocorre durante a execução do Pac-Mania. O áudio nunca é iniciado porque a interface `IMedia` nunca é obtida.

---

## 5. Comparação com Double Dragon (QUE FUNCIONA)

Double Dragon **não usa GetHandler**. Ele chama `CreateInstance` diretamente com o CLSID de áudio:

```
dd_smoke_full.log Line 293: ISHELL CreateInstance calling real_create_instance(0x01005511)
dd_smoke_full.log Line 295: IShell_CreateInstance(0x01005511) -> 0x10040E68
dd_smoke_full.log Line 296: IMedia_SetMediaData done: clsid=0x01005511 addr=0x1003A58C bytes=26822 rate=22050 ch=1 fmt=s16 vol=255
dd_smoke_full.log Line 297: IMedia_SetMediaParm: parm=16 val=0x00000001 (ignorado)
dd_smoke_full.log Line 298: IMedia_RegisterNotify: cb=0x0001D020 user=0x10001DAC
dd_smoke_full.log Line 299: IMedia_Play: clsid=0x01005511 voice=-1 addr=0x1003A58C bytes=26822 rate=22050 ch=1
dd_smoke_full.log Line 300: IMedia_Play: clsid=0x01005511 voice=0 addr=0x1003A58C bytes=26822 rate=22050 ch=1
dd_smoke_full.log Line 301: IMedia_notify: cmd=4 status=2 cb=0x0001D020 user=0x10001DAC
```

Sequência completa do Double Dragon:
1. `CreateInstance(0x01005511)` → obtém interface IMedia PCM
2. `IMedia_SetMediaData` → carrega dados de áudio WAV (26822 bytes, 22050 Hz, mono, s16)
3. `IMedia_RegisterNotify` → registra callback de notificação
4. `IMedia_Play` → inicia reprodução via `zaudio_play_pcm_ch()`, voz=0
5. `IMedia_notify` → confirma PLAY (cmd=4, status=2/DONE)

Resultado: `audio_nonzero=441000`, `audio_peak=1713` — **áudio presente**.

---

## 6. IMedia_Play Está Corretamente Implementado

`src/brew/imedia.c`, `case 6` (Play), linhas 364-417:

- Para **PCM**: chama `zaudio_play_pcm_ch(addr, bytes, rate, fmt, channels, loop, volume)` que inicia a reprodução real no mixer de áudio.
- Para **MIDI**: chama `zmidi_play_smf()`.
- Para **MP3**: chama `zmp3_play()`.
- Após tocar, envia notificação assíncrona via `media_notify()` com `MM_CMD_PLAY`.

A implementação do `IMedia_Play` está **correta e funcional** — é o mesmo caminho que o Double Dragon usa com sucesso.

---

## 7. real_create_instance Funciona Corretamente para MEDIAPCM_REAL

`src/brew/boot.c`, `real_create_instance()` (linhas 1421-1464):

```c
if (clsid == AEECLSID_MEDIAPCM_REAL)      // 0x01005511
    return zbrew_create_media_pcm();     // cria IMedia PCM funcional
if (clsid == AEECLSID_MEDIA_REAL || clsid == 0x01005500u)
    return zbrew_create_media_util();    // cria IMedia util (também funcional)
```

Ambos retornam interfaces `IMedia` completas, com vtable de 16 slots incluindo `AddRef`, `Release`, `QueryInterface`, `RegisterNotify`, `SetMediaData`, `GetMediaData`, `Play`, `Stop`, `Pause`, `Resume`, `GetState`, etc. (definidos em `make_media_interface`, `imedia.c` linhas 91-107).

Se o jogo chamasse `CreateInstance(0x01005511)`, receberia uma `IMedia` funcional e o áudio tocaria — exatamente como acontece com o Double Dragon.

---

## 8. MEDIA_REAL (0x01005500) vs MEDIAPCM_REAL (0x01005511)

Não é necessário tratamento especial entre esses dois CLSIDs para o `real_create_instance` — ambos são tratados e retornam interfaces `IMedia` funcionais:

| CLSID | Nome | Tratamento em real_create_instance | Resultado |
|-------|------|-----------------------------------|-----------|
| `0x01005500` | `AEECLSID_MEDIA_REAL` | `zbrew_create_media_util()` | IMedia funcional (stub util) |
| `0x01005511` | `AEECLSID_MEDIAPCM_REAL` | `zbrew_create_media_pcm()` | IMedia PCM funcional |

O problema **não é** qual CLSID o GetHandler retorna, mas sim que **ele não retorna nenhum CLSID em R0** — retorna 0 (status de sucesso) em vez do valor `0x01005511`.

---

## 9. CAUSA RAIZ

O `IShell_GetHandler` (slot 32) em `src/brew/boot.c`, linha 1675, define:

```c
g_cpu.r[0] = 0; /* SUCCESS */
```

Quando deveria ser:

```c
g_cpu.r[0] = handler_clsid; /* 0x01005511 = MEDIAPCM_REAL */
```

Fluxo quebrado:
1. Pac-Mania chama `IShell_GetHandler(0x01005500=MEDIA_REAL, mime=NULL)`
2. GetHandler reconhece MEDIA_REAL e define `handler_clsid = 0x01005511`
3. **Mas coloca 0 em R0 (sucesso) em vez de 0x01005511 (o CLSID)**
4. O jogo lê R0 = 0 e chama `CreateInstance(0)`
5. `CreateInstance(0)` cria um stub genérico (sem funcionalidade de áudio)
6. Nenhuma interface `IMedia` é obtida
7. Nenhuma chamada `IMedia_Play` / `IMedia_RegisterNotify` / `SetMediaData` ocorre
8. **Áudio silencioso**

---

## 10. Correção Necessária

Alterar em `src/brew/boot.c`, `case 32` de `zbrew_handle_ishell_real()`:

**Antes (linha 1675):**
```c
g_cpu.r[0] = 0; /* SUCCESS */
```

**Depois:**
```c
g_cpu.r[0] = handler_clsid; /* retorna o CLSID de mídia em R0 (convenção BREW/Zeemu) */
```

Opcionalmente, manter também a gravação em `r2` quando não-NULL (para compatibilidade com chamadas que passam ponteiro MIME):

```c
if (r2) zmem_write32(r2, handler_clsid);
g_cpu.r[0] = handler_clsid; /* CLSID de retorno em R0 */
```

Após essa correção, o Pac-Mania deverá chamar `CreateInstance(0x01005511)`, obter uma interface `IMedia` PCM funcional, e então chamar `IMedia_SetMediaData` + `IMedia_RegisterNotify` + `IMedia_Play` — reproduzindo áudio da mesma forma que o Double Dragon.

---

## 11. Arquivos Envolvidos

| Arquivo | Função | Problema |
|---------|--------|----------|
| `src/brew/boot.c` | `zbrew_handle_ishell_real()` case 32 (GetHandler) | Retorna 0 em R0 em vez do CLSID (0x01005511) |
| `src/brew/boot.c` | `real_create_instance()` | OK — trata MEDIAPCM_REAL e MEDIA_REAL corretamente |
| `src/brew/imedia.c` | `zbrew_handle_media()` case 6 (Play) | OK — IMedia_Play corretamente implementado (chama zaudio_play_pcm_ch) |
| `src/brew/imedia.c` | `make_media_interface()` | OK — vtable de 16 slots com todos os métodos de mídia |
| `src/audio/` | mixer, pcm | OK — backend de áudio funcional (usado pelo Double Dragon) |

---

## 12. Conclusão

O Pac-Mania ainda não tem áudio não porque o `IMedia` ou o `real_create_instance` estejam quebrados, mas porque o `IShell_GetHandler` (slot 32) **não retorna o CLSID em R0** — retorna apenas um código de sucesso (0). Como resultado, o jogo chama `CreateInstance(0)` em vez de `CreateInstance(0x01005511)`, nunca obtém uma interface `IMedia`, e nunca chama `IMedia_Play`. A correção é devolver `handler_clsid` (0x01005511) em `g_cpu.r[0]` no `case 32` de `zbrew_handle_ishell_real()`.
