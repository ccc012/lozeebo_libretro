# DDRENDER_2D — Como o Double Dragon renderiza gráficos

**Jogo:** Double Dragon (`tests/roms/ddragonz.mod`, CLSID do applet `0x0102F789`)  
**Build testada:** `build-smoke\Release\lozeebo_libretro.dll` + `libretro_smoke.exe`  
**Comando:** `$env:ZEEMU_STUB_THREAD=1; .\build-smoke\Release\libretro_smoke.exe .\build-smoke\Release\lozeebo_libretro.dll .\tests\roms\ddragonz.mod .\dd_render.ppm 120`  
**Autor:** investigação de renderização 2D (pt-BR)  
**Data:** 05/11/2026

---

## 1. Resumo executivo

Double Dragon **não utiliza desenho 2D via `IDisplay`**. O jogo:

- Inicializa `DISPLAY` (`0x01001001`) e pega o `device bitmap` via `IDisplay_GetDeviceBitmap` (slot 16).
- Inicializa **EGL** (`0x01014BC4`) e **OpenGL** (`0x01014BC3`), monta uma superfície EGL sobre o device bitmap e configura estado GL (`glShadeModel`, `glEnable`, `glDepthFunc`, …).
- **Nunca** chama `IDisplay_DrawText` (slot 4), `IDisplay_DrawRect` (slot 5), `IDisplay_BitBlt` (slot 6) nem `IDisplay_Update` (slot 7).
- **Nunca** chama `eglSwapBuffers`, `glClear` nem `glDrawArrays`/`glDrawElements`.
- **Não carrega** nenhum asset gráfico de disco (sem `.tga`, `.png`, `.bar`). Carrega apenas `sound.ggz` (áudio) e `./udata/ddz.sav` (save).
- **Não cria** thread de renderização separada; tudo roda no loop principal `HandleEvent` acionado por timers (33 ms + 16 ms).

Resultado do smoke test (120 frames):
```
[SMOKE-SUMMARY] video_frames=120 video_first=FFFFFF00 video_xor=00000000 video_varied=0
```
O framebuffer fica preso em `0xFFFFFF00` (amarelo sólido, vindo da pintura de diagnóstico em `eglCreateWindowSurface`) e **não varia** entre frames (`video_varied=0`). O pipeline de vídeo funciona (framebuffer é apresentado via `video_cb`), mas o jogo não produz conteúdo gráfico real.

> Essa conclusão está alinhada com o fix já documentado em `docs/DDRAGON_VIDEO_FIX.md` (auto-present via `zegl_handle(26)` = `eglSwapBuffers` + pintura amarela em `eglCreateWindowSurface`). O Double Dragon exibe tela amarela sólida em vez de preta, confirmando que o pipeline EGL → framebuffer → `video_cb` está funcionando. O que falta é conteúdo de jogo de verdade.

---

## 2. CLSIDs solicitados via `IShell_CreateInstance` (CreateInstance)

Todos os `CreateInstance` capturados no log do smoke test, mapeados com `src/brew/aee_ids.h`:

| CLSID | Nome (`aee_ids.h`) | Qtd. no log | Finalidade no Double Dragon |
|-------|--------------------|-------------|-----------------------------|
| `0x01001001` | `AEECLSID_DISPLAY_REAL` | 1 | Display principal (guardado em `g_display_obj`) |
| `0x01001002` | tratado como GRAPHICS em `real_create_instance` (`aee_ids.h` lista `AEECLSID_HEAP_REAL`) | Muitas (1/frame no loop de retry) | Objeto GRAPHICS (guardado em `g_graphics_obj`) |
| `0x0102F679` | `AEECLSID_FONT_STANDARD11` | 1 | Fonte 11 px |
| `0x01030852` | `AEECLSID_FONT_STANDARD15` | 1 | Fonte 15 px |
| `0x0102F681` | `AEECLSID_FONT_STANDARD36` | 1 | Fonte 36 px |
| `0x01002001` | `AEECLSID_GRAPHICS_REAL` | 1 | Graphics real |
| `0x01001003` | `AEECLSID_FILEMGR_REAL` | 1 | Gerenciador de arquivos (abre `ddz.sav`, `sound.ggz`) |
| `0x01001014` | *(não mapeado em `aee_ids.h`)* | 1 | Desconhecido (objeto `0x10005FD8`) |
| `0x01014BC3` | `AEECLSID_GL_REAL` | 1 | OpenGL |
| `0x01014BC4` | `AEECLSID_EGL_REAL` | 1 | EGL |
| `0x01005511` | `AEECLSID_MEDIAPCM_REAL` | 1 | Áudio PCM (`IMedia_SetMediaData` / `IMedia_Play`) |
| `0x0106C411` | `AEECLSID_HID_REAL` | 1 | Entrada (HID) |
| `0x01041207` | `AEECLSID_SIGNALCBFACT_R` | 1 | `SignalCBFactory` (sinais de callback) |
| `0x4449424D` ('DIBM') | `ZCLSID_DEVICE_BITMAP` | via `GetDeviceBitmap` | Device bitmap (retornado por `IDisplay_GetDeviceBitmap`) |

**O `real_create_instance` (`src/brew/boot.c:1421`) trata explicitamente** `0x01001001` e `0x01001002` como DISPLAY/GRAPHICS (linhas 1445-1453), cria `EGL_REAL` (1427-1428), `GL_REAL` (1429-1430), mídias PCM/ADPCM/MIDI/MP3 (1433-1442) e tem um branch para `AEECLSID_IMAGE` (1454-1456) — mas esse branch nunca é alcançado pelo Double Dragon (ver seção 3).

---

## 3. IImage / `AEECLSID_IMAGE` — NÃO é usado

- `src/brew/iimage.h` define `#define AEECLSID_IMAGE 0x0100100Cu` (comentário: "conjectura: mesmo que o Double Dragon usa").
- `real_create_instance` tem o branch `if (clsid == AEECLSID_IMAGE) return zimage_create(clsid);` (`boot.c:1544`).
- **No log do Double Dragon não há nenhum `ISHELL CreateInstance` para `0x0100100C`** nem para qualquer outro CLSID relacionado a imagem.
- Conclusão: o jogo **não cria e não usa `IImage`/`IImage`**. Toda a referência a `IDisplay` no log é unicamente `IDisplay_GetDeviceBitmap`, que **não é** uma operação de desenho.

---

## 4. Operações de desenho `IDisplay` — esperadas vs. usadas

Layout dos slots da vtable `IDisplay` (conforme `src/brew/boot.c` e `docs/STATUS_R5_B_IDISPLAY.md`, espelhando `BrewDisplay::setup_vtable()` do zeemu):

| Slot | Operação | Chamado pelo Double Dragon? | Observação |
|------|----------|:---------------------------:|------------|
| 0 | `AddRef` | Sim (stub) | Contagem de referência |
| 1 | `Release` | Sim (stub) | Contagem de referência |
| 2 | `GetFontMetrics` | Não | — |
| 3 | `MeasureTextEx` | Não | — |
| **4** | **`DrawText`** | **NÃO** | Nunca chamado |
| **5** | **`DrawRect`** | **NÃO** | Nunca chamado |
| **6** | **`BitBlt`** | **NÃO** | Nunca chamado |
| 7 | `Update` | Não (no DISPLAY) | slot 7 aparece apenas no objeto GRAPHICS/HID (stubs) |
| 8 | `SetAnnunciators` | Não | — |
| 9 | `Backlight` | Não | — |
| 10 | `SetColor` | Não | — |
| 11 | `GetSymbol` | Não | — |
| 12 | `DrawFrame` | Não | — |
| 13 | `CreateDIBitmap` | Não | — |
| 14 | `SetDestination` | Não | — |
| 15 | `GetDestination` | Não | — |
| **16** | **`GetDeviceBitmap`** | **SIM (2x)** | Retorna device bitmap `0x10005F08` (`DIBM`) |
| 17 | `SetFont` | Não | — |
| 18 | `SetClipRect` | Não | — |
| 19 | `GetClipRect` | Não | — |

**Conclusão:** das operações de desenho 2D (`DrawText`, `DrawRect`, `BitBlt`, `Update`, `CreateDIBitmap`, `SetDestination`), **nenhuma é invocada**. A única interação real com `IDisplay` é `GetDeviceBitmap`.

---

## 5. O que o jogo faz com o device bitmap (`GetDeviceBitmap`)

1. Chama `IDisplay_GetDeviceBitmap` (slot 16) sobre `clsid=0x01001001`, duas vezes no log (linhas ~87-89 e ~107-108).
2. Recebe o objeto device bitmap `0x10005F08` (classe `0x4449424D` = 'DIBM').
3. Chama `DIBM` slot=1 (`AddRef`) e slot=2 (`QueryInterface`, `r1=0x01001045`).
4. Passa o device bitmap para o EGL: `eglCreateWindowSurface` (EGL slot 12, `r2 = 0x10005F08` = o objeto DIBM).
5. O framebuffer é pintado de amarelo (`0xFFFFFF00`) uma única vez após `eglCreateWindowSurface` (diagnóstico `EGL_SURFACE_DIAG`).
6. No loop de jogo, `DIBM` slot=1 (`AddRef`) é chamado novamente (contagem de referência / recriação de superfície).

Interpretação: o device bitmap é usado apenas como **suporte para a superfície EGL**, não como alvo de desenho 2D direto.

---

## 6. Caminho de renderização real: EGL + OpenGL (não 2D)

O Double Dragon é um jogo **EGL/OpenGL**, não um jogo de desenho 2D via `IDisplay`:

- Cria `EGL_REAL` (`0x01014BC4`) e `GL_REAL` (`0x01014BC3`).
- Inicializa EGL nos slots 4, 5, 10, 12, 17, 19 (incluindo `eglCreateWindowSurface` no slot 12, passando o device bitmap).
- Chama estado GL: `glShadeModel`, `glHint`, `glEnable`, `glDepthFunc`, `glCullFace`, `glAlphaFuncx`, `glPixelStorei`, `glDisable`.
- **Não chama** `glClear`, `glDrawArrays`, `glDrawElements`, `eglSwapBuffers`, `eglMakeCurrent` (para renderização).

Como nunca emite comandos de desenho nem troca de buffer por conta própria, o conteúdo da tela depende do emulador apresentar o framebuffer. Foi exatamente o que o fix em `docs/DDRAGON_VIDEO_FIX.md` faz:
- `src/core/libretro_core.c`: quando `zegl_has_context()` é verdadeiro, chama `zegl_handle(26)` (`eglSwapBuffers`) para apresentar o framebuffer mesmo sem o jogo chamar swap.
- `src/gpu/egl_gl.c`: pinta o framebuffer de amarelo brilhante em `eglCreateWindowSurface` (slots 12-14) para confirmar que o pipeline funciona.

Resultado atual: tela amarela sólida (`0xFFFFFF00`), `video_varied=0` — pipeline OK, mas sem conteúdo de jogo variando.

---

## 7. Assets carregados do disco

No log do smoke test, os únicos arquivos abertos via `IFileMgr` são:

| Arquivo | Tamanho / detalhe | Finalidade |
|---------|-------------------|------------|
| `./udata/ddz.sav` | 180 bytes | Save / estado do jogo |
| `sound.ggz` | 1.940.815 bytes, 74 membros, TOC em 592 | Áudio (GGZ) |
| PCM via `IMedia` (`0x01005511` = `AEECLSID_MEDIAPCM_REAL`) | 26.822 bytes, 22050 Hz, mono, s16 | Áudio PCM tocado via `IMedia_SetMediaData` + `IMedia_Play` |

**Não há carregamento de `.tga`, `.png` nem `.bar`.** O jogo não carrega nenhum asset gráfico de disco em toda a janela de 120 frames do smoke test. Isso reforça que os gráficos viriam de desenho procedural (EGL/GL) ou de assets empacotados internamente no `.mod` que nunca são alcançados porque o jogo não avança além da inicialização/EGC.

---

## 8. Thread de renderização

- **Não existe thread de renderização separada.** Nenhum `CreateInstance` para `AEECLSID_THREAD_REAL` (`0x01001017`) aparece no log.
- Tudo roda no **loop principal `HandleEvent`** (`0x00000488`), acionado por timers:
  - timer de 33 ms após `EVT_APP_START` (inicialização),
  - timer de 16 ms (game loop, `zboot_tick` / `zboot_process_timers`).
- A variável de ambiente `ZEEMU_STUB_THREAD=1` é setada, mas nenhum objeto de thread é criado pelo jogo.

---

## 9. O que precisa ser implementado para o Double Dragon mostrar gráficos

Dado que o jogo **não usa `IDisplay` para desenho**, implementar apenas `DrawText`/`DrawRect`/`BitBlt` **não é suficiente** para este jogo. O caminho necessário é o de EGL/OpenGL:

1. **Confirmar/manter o auto-present do EGL** — `zegl_handle(26)` (`eglSwapBuffers`) chamado pelo emulador quando houver contexto EGL e o jogo não chamar swap por conta própria (já implementado em `docs/DDRAGON_VIDEO_FIX.md`). Sem isso, a tela fica preta.
2. **Fazer o jogo emitir comandos de desenho GL** — atualmente ele configura estado GL mas nunca chama `glClear`/`glDrawArrays`/`eglSwapBuffers`. Duas possibilidades:
   - o jogo ainda está preso antes do gameplay (loop de retry/inicialização) e só emitiria draw commands após avançar; ou
   - o jogo espera que o emulador apresente automaticamente a superfície EGL (o que o auto-present já tenta fazer).
3. **Conteúdo gráfico real** — como não há assets `.tga`/`.png`/`.bar` carregados, qualquer imagem na tela viria de:
   - desenho procedural via OpenGL (triângulos/texturas empacotadas no `.mod`), ou
   - dados de pixels gravados diretamente no device bitmap / VRAM (caminho que ainda não é explorado).
4. **Validar visualmente** — após o auto-present, o smoke test mostra `video_first=FFFFFF00` e `video_varied=0`. Para considerar o jogo "mostrando gráficos", seria necessário `video_varied > 0` (pixels diferente entre frames/regiões), o que só acontece quando o jogo desenha conteúdo de fato.

---

## 10. Referências

- `docs/DDRAGON_VIDEO_FIX.md` — fix de vídeo já aplicado (auto-present e pintura amarela).
- `docs/STATUS_A_EVENTO_DOUBLE_DRAGON.md` — event loop e timers do Double Dragon.
- `docs/STATUS_R5_B_IDISPLAY.md` — implementação dos slots `IDisplay` (DrawText/BitBlt/CreateDIBitmap/SetDestination).
- `src/brew/boot.c` — `real_create_instance` (CLSIDs), dispatch de slots `IDisplay` (DrawText slot 4, DrawRect slot 5, BitBlt slot 6, Update slot 7, GetDeviceBitmap slot 16).
- `src/brew/aee_ids.h` — tabela de CLSIDs AEE.
- `src/brew/iimage.h` / `src/brew/iimage.c` — `AEECLSID_IMAGE` e `IImagem` (não usados pelo jogo).
- Log do smoke test: saídas de `libretro_smoke.exe` com `ZEEMU_STUB_THREAD=1` (framebuffer `FFFFFF00`, `video_varied=0`).
