# SUPBTIME — Investigação de vídeo (tela preta)

Jogo: **Super BurgerTime** (`tests/roms/supbtime_mod.mod`, CLSID do applet `0x010924E3`)

---

## 1. Resultado do smoke test

Comando executado:

```
$env:ZEEMU_STUB_THREAD=1
.\build-smoke\Release\libretro_smoke.exe .\build-smoke\Release\lozeebo_libretro.dll .\tests\roms\supbtime.mod .\supbtime_test.ppm 300
```

Log completo gravado em `supbtime_run.log`.

**[SMOKE-SUMMARY] video_frames=300 video_first=00000000 video_xor=00000000 video_varied=0 audio_frames=220500**

- `video_first=00000000`: primeiro frame é totalmente preto.
- `video_xor=00000000`: o framebuffer não contém nenhum conteúdo (todos os pixels iguais a preto).
- `video_varied=0`: não há variação de pixel dentro do frame.
- Conclusão: **tela preta confirmada**. Nenhum desenho chega ao framebuffer.

O único registro de vídeo foi `[VIDEO] 640x480 pitch=2560 first=0x00000000` (frame 0). Depois disso, o jogo entra em loop de `suspend enter` e nunca mais produz frame.

---

## 2. video_varied e video_xor

Essas métricas são calculadas pelo smoke test (`tests/libretro_smoke.c`, função `video`):

- `video_xor`: XOR acumulado de `row[x] + x + y * width` sobre todos os pixels. Para um framebuffer sólido (tela preta ou cor única) resulta em `00000000`.
- `video_varied`: verdadeiro se algum pixel difere do primeiro pixel do frame. Indica se há conteúdo gráfico real.

Para Supbtime ambos são zero: o framebuffer permanece preto e uniforme durante os 300 frames. Não há desenho de jogo.

---

## 3. Todas as chamadas CreateInstance — CLSIDs usados pelo Supbtime

Extraídas de `supbtime_run.log` (função `real_create_instance` em `src/brew/boot.c`):

| CLSID | Nome (aee_ids.h) | Usado pelo Supbtime? | Observação |
|-------|------------------|----------------------|------------|
| `0x010924E3` | Super BurgerTime (applet) | Sim | Próprio applet |
| `0x01001001` | `AEECLSID_DISPLAY_REAL` | **Sim** | DISPLAY criado em `0x10001528`, gravado em `applet+0x68` |
| `0x01001003` | `AEECLSID_FILEMGR_REAL` | Sim | Abre `font.fnz` (fails — não encontrado no VFS) |
| `0x0103D8EC` | `AEECLSID_QEGL_REAL` | **Sim** | EGL (QEGL) criado em `0x1002D7E8` |
| `0x010127D4` | `AEECLSID_DISPLAY1_REAL` | Sim | CRIADO (`0x1002DAE0`), mas não usado para desenho |
| `0x0106C411` | `AEECLSID_HID_REAL` | Sim | HID (joystick) |
| `0x01041207` | `AEECLSID_SIGNALCBFACT_R` | Sim | SignalCBFactory |
| `0x01001017` | `AEECLSID_THREAD_REAL` | Sim | Thread (game loop 16 ms) |

**Resumo:** DISPLAY (`0x01001001`) e EGL (`0x0103D8EC`) são instanciados. Nenhum outro CLSID de vídeo/gráficos é usado.

> Nota: o DISPLAY entregue ao Supbtime é um **stub genérico** (`make_stub_interface`), não o `IDisplay` real com métodos de desenho (`idisplay.c`, `ZT_DISP_*`). O stub apenas retorna sucesso. Mesmo que o jogo chamasse operações de desenho, elas seriam no-ops.

---

## 4. DISPLAY, GRAPHICS, EGL, GL, IImage

| Recurso | Usado? | Evidence no log |
|---------|--------|-----------------|
| **DISPLAY** (`0x01001001`) | **Sim** | `real_create_instance: DISPLAY/GRAPHICS matched clsid=0x01001001`; `applet+0x68 = DISPLAY 0x10001528` |
| **GRAPHICS** (`0x01002001`) | Não | Nunca solicitado |
| **EGL** (`0x0103D8EC` / QEGL_REAL) | **Sim** | `IQEGL_Initialize`, `IQEGL_ChooseConfig`, `IQEGL_slot11(CreateGL)`, `IQEGL_slot18`, `IQEGL_QueryInterface` para várias interfaces GLES |
| **GL** (`0x01014BC3` / GL_REAL) | Não | Nunca solicitado (`0x010127D4` é DISPLAY1_REAL, não GL) |
| **IImage** (`AEECLSID_IMAGE`) | Não | Nunca solicitado (`zimage_create` não é chamado) |

Conclusão: Supbtime usa **DISPLAY + EGL**, mas não usa GRAPHICS, GL nem IImage.

---

## 5. Valor de applet+0x6C

No dump do applet (`obj=0x100014E8`) gravado em `boot.c` após `IModule_CreateInstance`:

```
boot:   applet+0x68 = DISPLAY 0x10001528
...
boot: applet=0x100014E8 vtbl=0x10001508 HandleEvent=0x0000B5B4 obj+0x18=0x0001BD88 obj+0x6C=0x00000000
```

- `applet+0x6C = 0x00000000` (render_fn / gfx_handler não preenchido).
- Esse campo é o ponteiro de callback de renderização. No código atual (`boot.c`, `BOOT_CREATE_APPLET`), ele só é injetado para RE4 (`clsid == 0x0108AF6C`). Para todos os outros jogos (incluindo Supbtime) fica em zero, à espera que o jogo preencha — o que nunca acontece porque o jogo fica preso no loop de suspend.

---

## 6. Supbtime chama operações IDisplay?

**Parcialmente, e nenhuma de desenho:**

Do log (`DIBMSTUB` / `zbrew_handle_stub`):

- `slot=16` (`IDisplay_GetDeviceBitmap`) **é chamado** — retorna bitmap `0x1002D798` (linhas 111-113, 133-134, 148-149).
- Nenhuma operação de desenho é chamada para o display do Supbtime: sem `BITBLT`, `FILLRECT`, `CLEARSCREEN`, `DRAWRECT`, `DRAWLINE`, `DRAWPIXEL` nem `UPDATE`.

Como o display é um stub genérico, mesmo o `GetDeviceBitmap` não produz pixels reais. O framebuffer nunca é preenchido por IDisplay.

---

## 7. Supbtime usa EGL?

**Sim, inicializa EGL completamente, mas nunca chama `eglSwapBuffers`.**

Do log (`supbtime_run.log`):

- `real_create_instance: clsid=0x0103D8EC` → QEGL_REAL criado em `0x1002D7E8`.
- `IQEGL_Initialize` (slot 5), `IQEGL_ChooseConfig` (slot 9), `IQEGL_slot11(CreateGL)` (slot 11), `IQEGL_slot16` (surface), `IQEGL_slot18` (contexto).
- `glGetString` (pname `0x1F03` = extensões GL), `glEnable` (`0xDE1`).
- Várias `IQEGL_QueryInterface` para IIDs GLES (`0x0103D8DD/DE/EB/EF/F0`, `0x010426E3`, `0x01051834`, `0x01058546`, `0x0103DEF1`).

Porém **`eglSwapBuffers` nunca é invocado**, então a superfície EGL nunca é apresentada ao framebuffer do libretro.

O fallback de auto-apresentação em `src/core/libretro_core.c` (`retro_run`, linhas ~534-540) só dispara quando `!zbrew_frame_ready() && !g_cpu.halted`. Como Supbtime fica com `g_cpu.halted = true` (aguardando timers/eventos) entre ticks, o fallback não executa e nenhum frame é enviado ao frontend.

---

## 8. Assets carregados do disco

Supbtime tenta abrir um único arquivo pelo `IFileMgr` (`0x01001003`):

```
IFileMgr_OpenFile: 'font.fnz' R1=0x2FFFFE30 modo=0x1
VFS: data.vfs carregado (448 membros, 21730345 bytes)
IFileMgr_OpenFile: 'font.fnz' (modo 0x1) nao encontrado
DBGPRINTF: Failed to open font.fnz!!!
```

- O VFS (`data.vfs`) tem 448 membros, mas **`font.fnz` não está nele**.
- A falha de abertura da fonte causa falha de alocação (`zheap_alloc: sem memoria para 268626016 bytes`), `HLE uncompress FAIL`, e o jogo fica preso no loop `DBGPRINTF: suspend enter` chamando `HandleEvent(EVT_APP_SUSPEND, r1=2)` repetidamente.
- Como nunca sai da fase de carregamento/suspensão, nunca chega a registrar o callback de render (`applet+0x6C`), nunca chama operações de desenho IDisplay e nunca chama `eglSwapBuffers`.

---

## Causa raiz da tela preta

Supbtime não desenha porque:

1. **`applet+0x6C = 0`**: nenhum callback de renderização é injetado (o código só injeta para RE4) e o jogo não o preenche.
2. **Loop de suspend**: `font.fnz` não existe no `data.vfs`, o jogo trava na fase de carregamento e fica chamando `HandleEvent(EVT_APP_SUSPEND)` em loop (`suspend enter`), sem avançar para o desenho.
3. **Sem operações de desenho IDisplay**: apenas `GetDeviceBitmap` (stub) é chamado; nenhum `BITBLT`/`FILLRECT`/`CLEARSCREEN`/`UPDATE`.
4. **Sem `eglSwapBuffers`**: EGL é inicializado, mas nunca apresentado.
5. **Fallback de auto-apresentação bloqueado**: `g_cpu.halted = true` impede o `mark_frame` + `eglSwapBuffers` forçado em `libretro_core.c`.

Resultado: framebuffer permanece em `0x00000000` (preto) → `video_xor=00000000`, `video_varied=0`.

---

## Caminhos sugeridos para correção

- **Carregamento da fonte**: adicionar `font.fnz` ao `data.vfs` ou fazer `IFileMgr_OpenFile` retornar um stub/vazio para arquivos ausentes, evitando o loop de suspend e a falha de alocação de 256 MB.
- **Render callback (`applet+0x6C`)**: injetar um dispatcher de renderização genérico (análogo ao feito para RE4 em `0x2F440`) para jogos que usam EGL/IDisplay mas não preenchem `+0x6C`, de modo que o game loop chame desenho/swap a cada tick.
- **Auto-apresentação**: permitir o `mark_frame`/`eglSwapBuffers` forçado em `libretro_core.c` mesmo quando `g_cpu.halted` for verdade, desde que o jogo esteja em `BOOT_RUNNING` e haja contexto EGL (evita tela preta indefinida em jogos travados entre ticks).
- **IDisplay real**: considerar entregar o `IDisplay` real (`idisplay.c`, `zbrew_handle_display`) em vez do stub genérico para `AEECLSID_DISPLAY_REAL`, para que operações de desenho futuro tenham efeito no framebuffer.
