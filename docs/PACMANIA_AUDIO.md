# PAC-MANIA - Ausência de Áudio: Investigação e Causa Raiz

**Data:** 05/11/2026  
**Jogo:** Pac-Mania (CLSID: `0x01087B72`, arquivo: `tests/roms/real_pacmania_game.mod`)  
**Status:** Vídeo OK (`video_varied=1`), áudio silencioso (`audio_nonzero=0`, `audio_peak=0`)  
**Comparação:** Double Dragon funciona com áudio ADPCM (`audio_nonzero=441000`, `audio_peak=1713`)

---

## 1. Smoke Test Executado

Comando executado:

```powershell
$env:ZEEMU_STUB_THREAD=1; .\build-smoke\Release\libretro_smoke.exe .\build-smoke\Release\lozeebo_libretro.dll .\tests\roms\real_pacmania_game.mod .\pac_test.ppm 300
```

Resultado:

```
[SMOKE-SUMMARY] video_frames=300 video_first=FF000000 video_xor=0023D2E0 video_varied=1 audio_frames=220500 audio_nonzero=0 audio_peak=0
```

- **Vídeo:** 300 frames, variado — OK
- **Áudio:** 220500 frames gerados, mas todos zero (silêncio absoluto) — FALHA

Para comparação, Double Dragon:

```
[SMOKE-SUMMARY] video_frames=300 ... audio_frames=220500 audio_nonzero=441000 audio_peak=1713
```

---

## 2. Todos os CreateInstance Chamados pelo Pac-Mania

Extraídos do log `pacmania_audio_trace.log` (trace com `ZEEMU_TRACE_HID=1`), o jogo chama `IShell_CreateInstance` com os seguintes CLSIDs:

| # | CLSID | Nome | Resultado |
|---|-------|------|-----------|
| 1 | `0x01001001` | `AEECLSID_DISPLAY_REAL` | Display stub |
| 2 | `0x01001002` | `AEECLSID_HEAP_REAL` / `GRAPHICS` | Graphics stub |
| 3 | `0x01001003` | `AEECLSID_FILEMGR_REAL` | FileMgr |
| 4 | `0x0106C411` | `AEECLSID_HID_REAL` | HID stub |
| 5 | `0x01041207` | `AEECLSID_SIGNALCBFACT_R` | SignalCBFact stub |
| 6 | `0x0103D8EC` | `AEECLSID_QEGL_REAL` | QEGL interface |
| 7 | `0x010127D4` | `AEECLSID_DISPLAY1_REAL` | Display1 stub |
| 8 | `0x01001056` | (desconhecido) | stub genérico |
| 9 | `0x00000000` | (nulo) | stub genérico (várias vezes) |

**Nenhum CLSID de áudio é solicitado via `CreateInstance`.**

---

## 3. CLSIDs de Áudio Disponíveis (Nenhum é Usado pelo Pac-Mania)

Definidos em `src/brew/aee_ids.h`:

| CLSID | Nome | Usado por Pac-Mania? |
|-------|------|---------------------|
| `0x01005511` | `AEECLSID_MEDIAPCM_REAL` | **Não** |
| `0x0100550A` | `AEECLSID_MEDIAADPCM_REAL` | **Não** |
| `0x01005502` | `AEECLSID_MEDIAMP3_REAL` | **Não** |
| `0x01005501` | `AEECLSID_MEDIAMIDI_REAL` | **Não** |
| `0x0100550D` | `AEECLSID_MEDIAUTIL_REAL` | **Não** |
| `0x01002000` | `AEECLSID_SOUNDPLAYER_R` | **Não** |
| `0x01001005` | `AEECLSID_SOUND` | **Não** |
| `0x01005500` | `AEECLSID_MEDIA_REAL` (genérico) | Usado via `GetHandler`, não via `CreateInstance` |

O Pac-Mania **não chama** `CreateInstance` com nenhum desses CLSIDs.

---

## 4. Interface ISound — `src/brew/isound.c`

Existe e está parcialmente implementada:

- `AEECLSID_SOUND (0x01001005)` é criado por `create_by_clsid()` em `src/brew/ishell.c` com uma vtable de **15 slots**:
  - [0] AddRef, [1] Release, [2] RegisterNotify, [3] Set, [4] Get,
  - [5] SetDevice, [6] PlayTone, [7] PlayToneList, [8] PlayFreqTone,
  - [9] StopTone, [10] Vibrate, [11] StopVibrate, [12] SetVolume,
  - [13] GetVolume, [14] GetResourceCtl

- O handler `zbrew_handle_sound()` em `isound.c` trata todos os slots como **stubs** que apenas retornam sucesso (`0`) — não há geração real de áudio.

- O Pac-Mania **não solicita** `AEECLSID_SOUND`, então ISound não é o caminho usado por este jogo.

---

## 5. Interface IMedia — `src/brew/imedia.c`

Está **corretamente implementada** para PCM, ADPCM, MP3 e MIDI:

- `zbrew_create_media_pcm()` -> `AEECLSID_MEDIAPCM_REAL`
- `zbrew_create_media_adpcm()` -> `AEECLSID_MEDIAADPCM_REAL`
- `zbrew_create_media_midi()` -> `AEECLSID_MEDIAMIDI_REAL`
- `zbrew_create_media_mp3()` -> `AEECLSID_MEDIAMP3_REAL`
- `zbrew_create_media_util()` -> `AEECLSID_MEDIAUTIL_REAL` e `AEECLSID_SOUNDPLAYER_R`

Todos são tratados por `real_create_instance()` em `src/brew/boot.c` (linhas 1419-1430).

**Porém**, `real_create_instance` **não trata** `AEECLSID_MEDIA_REAL (0x01005500)` — o CLSID genérico de mídia. Ele só trata os CLSIDs de formato específico (PCM, ADPCM, MP3, MIDI).

---

## 6. Comparação com Double Dragon (QUE FUNCIONA)

Double Dragon usa um caminho diferente:

```
ISHELL: slot=2 (CreateInstance) R1=0x01005511   <- AEECLSID_MEDIAPCM_REAL
real_create_instance: clsid=0x01005511
IShell_CreateInstance(0x01005511) -> 0x10040E68
```

Double Dragon chama **diretamente** `CreateInstance(AEECLSID_MEDIAPCM_REAL)`, que é tratado por `real_create_instance` -> `zbrew_create_media_pcm()`. O áudio PCM é então decodido e mixado normalmente.

Resultado DD: `audio_nonzero=441000`, `audio_peak=1713` — **áudio presente**.

---

## 7. CAUSA RAIZ: `IShell_GetHandler` (Slot 32) Não Implementado

O Pac-Mania **não usa** `CreateInstance` com CLSIDs de áudio. Em vez disso, ele chama:

```
ISHELL: slot=32 (GetHandler) R0=0x10001270 R1=0x01005500 R2=0x00000000
```

Isso é `IShell_GetHandler(AEECLSID_MEDIA_REAL = 0x01005500, mime=NULL)`.

### Referência Zeemu (`reference/zeemu-main/zeemu-main/brew/BrewShell.cpp`):

```cpp
// Linha 1232-1246
else if (hook.name == "IShell_GetHandler" || (!is_helper_hook && call_idx == 32)) {
    const uint32_t handler_type = ... r1;   // 0x01005500 = AEECLSID_MEDIA
    const addr_t psz_in = ... r2;            // ponteiro para string MIME
    const std::string mime = read_guest_text(psz_in, 128);
    uint32_t cls = 0;
    if (handler_type == 0x01005500) { // AEECLSID_MEDIA
        cls = media_handler_for_mime(mime);  // mapeia MIME -> CLSID específico
    }
    cpu.set_reg(REG_R0, cls);  // retorna o CLSID de mídia específico
}
```

A função `media_handler_for_mime()` mapeia:
- `audio/wav` / `audio/wave` -> `0x0100550a` (AEECLSID_MEDIAADPCM)
- `audio/mp3` / `audio/mpeg` -> `0x01005502` (AEECLSID_MEDIAMP3)
- `audio/mid` / `audio/midi` -> `0x01005501` (AEECLSID_MEDIAMIDI)

### Problema no código atual:

Em `src/brew/boot.c`, a função `zbrew_handle_ishell_real()` **não possui um `case` para slot 32** (`GetHandler`). O switch trata slots 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 14, 15, 16, 17, 18, 19, 20 — **mas não o 32**.

Consequência: quando o Pac-Mania chama `IShell_GetHandler(0x01005500, NULL)`, o handler não faz nada, não define `R0` (retorna valor indefinido/zero), e o jogo não consegue obter um CLSID de mídia para criar a interface `IMedia`. Sem a interface `IMedia`, não há chamada `IMedia_Play()`, e portanto **sem áudio**.

O mesmo ocorre em 3 pontos do log (linhas ~1293, ~1354, ~1414), cada um seguido por uma mensagem de debug de `ZeeboSound.cpp:144` — indicando que o jogo tenta inicializar áudio via `GetHandler`, falha, e imprime mensagens de erro.

---

## 8. Segunda Lacuna: `AEECLSID_MEDIA_REAL` Não Tratado em `real_create_instance`

Mesmo que `GetHandler` fosse implementado e retornasse um CLSID, o jogo poderia então chamar `CreateInstance` com o CLSID retornado (ex.: `AEECLSID_MEDIAADPCM_REAL`). Isso já é tratado por `real_create_instance`.

Porém, se o jogo chamar `CreateInstance(AEECLSID_MEDIA_REAL = 0x01005500)` diretamente (o CLSID genérico de mídia), `real_create_instance` não tem um `case` para ele — cairia em `make_stub_interface(clsid)`, criando um stub genérico sem funcionalidade de áudio.

---

## 9. Resumo do Problema

| Item | Pac-Mania | Double Dragon |
|------|-----------|---------------|
| Caminho de áudio | `IShell_GetHandler(AEECLSID_MEDIA_REAL)` | `CreateInstance(AEECLSID_MEDIAPCM_REAL)` |
| `GetHandler` (slot 32) | **Não implementado** | Não usado |
| `CreateInstance` com CLSID de áudio | Não chama | Chama `0x01005511` (PCM) |
| `real_create_instance` trata CLSID genérico de mídia | **Não trata `0x01005500`** | Não necessário |
| `IMedia` interface obtida | Não | Sim (via PCM) |
| Áudio | Silêncio (`audio_nonzero=0`) | Funciona (`audio_nonzero=441000`) |

---

## 10. Correções Necessárias

### Correção 1: Implementar `IShell_GetHandler` (slot 32) em `src/brew/boot.c`

Adicionar um `case 32` no `switch` de `zbrew_handle_ishell_real()` que:

1. Leia `R1` = `handler_type` (CLSID do serviço solicitado)
2. Leia `R2` = ponteiro para string MIME (pode ser NULL)
3. Se `handler_type == AEECLSID_MEDIA_REAL (0x01005500)`:
   - Se MIME for fornecido, mapeie para o CLSID específico (ex.: `audio/wav` -> `AEECLSID_MEDIAADPCM_REAL`)
   - Se MIME for NULL, retorne um padrão sensato (ex.: `AEECLSID_MEDIAPCM_REAL` ou `AEECLSID_MEDIAADPCM_REAL`)
4. Escreva o CLSID resultante em `R0`

### Correção 2: Tratar `AEECLSID_MEDIA_REAL (0x01005500)` em `real_create_instance()`

Adicionar em `src/brew/boot.c`, função `real_create_instance()`:

```c
if (clsid == AEECLSID_MEDIA_REAL)    /* 0x01005500 */
    return zbrew_create_media_pcm();  /* ou adpcm, como padrão */
```

Isso garante que, se o jogo chamar `CreateInstance` com o CLSID genérico de mídia, ele receba uma interface `IMedia` funcional em vez de um stub.

---

## 11. Arquivos Envolvidos

| Arquivo | Função | Problema |
|---------|--------|----------|
| `src/brew/boot.c` | `zbrew_handle_ishell_real()` | Falta `case 32` (GetHandler) |
| `src/brew/boot.c` | `real_create_instance()` | Falta `case AEECLSID_MEDIA_REAL (0x01005500)` |
| `src/brew/isound.c` | `zbrew_handle_sound()` | ISound é stub (não usado por Pac-Mania) |
| `src/brew/imedia.c` | `zbrew_handle_media()` | **OK** — IMedia está corretamente implementado para PCM/ADPCM/MP3/MIDI |
| `src/brew/ishell.c` | `create_by_clsid()` | OK — trata SHELL, DISPLAY, GRAPHICS, BITMAP, FILEMGR, SOUND |
| `src/audio/` | mixer, pcm, adpcm, mp3, midi | **OK** — backend de áudio funcional |

---

## 12. Conclusão

O Pac-Mania não tem áudio porque chama `IShell_GetHandler(AEECLSID_MEDIA_REAL)` via slot 32, uma chamada que **não está implementada** no emulador. Sem um handler de mídia retornado, o jogo nunca cria uma interface `IMedia` e nunca chama `IMedia_Play()`. O Double Dragon funciona porque usa diretamente `CreateInstance(AEECLSID_MEDIAPCM_REAL)`, que já é tratado.

A correção requer implementar `IShell_GetHandler` (slot 32) e tratar `AEECLSID_MEDIA_REAL` em `real_create_instance`, permitindo que o Pac-Mania obtenha uma interface `IMedia` e reproduza áudio PCM/ADPCM como o Double Dragon já faz.