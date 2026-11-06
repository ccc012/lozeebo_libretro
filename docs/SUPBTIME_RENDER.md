# SUPBTIME_RENDER.md — Investigação de renderização do Super BurgerTime

**Projeto:** zeebo_libretro  
**ROM:** `tests/roms/supbtime.mod` (CLSID do applet: `0x010924E3` — "Super BurgerTime", conferido em `src/brew/aee_ids.h` linha 129)  
**Data:** 05/11/2026  
**Status:** Tela preta, sem desenho. DISPLAY vinculado, timer rodando, EGL inicializado, mas nada é desenhado.

---

## 1. Smoke test (resultado)

Comando executado:

```powershell
cd C:\Users\Lucas\source\repos\zeebo_libretro
$env:ZEEMU_STUB_THREAD=1
.\build-smoke\Release\libretro_smoke.exe .\build-smoke\Release\lozeebo_libretro.dll .\tests\roms\supbtime.mod .\supbtime_test.ppm 300
```

Resultado do resumo final (`[SMOKE-SUMMARY]`):

```
video_frames=300 video_first=00000000 video_xor=00000000 video_varied=0
audio_frames=220500 audio_nonzero=0 audio_peak=0
```

Interpretação:

| Métrica | Valor | Significado |
|---------|-------|-------------|
| `video_varied` | **0** | O framebuffer nunca muda entre frames (tela estática) |
| `video_first` | `00000000` | Primeiro frame é todo zeros (preto) |
| `video_xor` | `00000000` | Nenhuma variação de pixels entre frames |
| `audio_nonzero` | **0** | Sem áudio produzido |

O jogo entra num **loop infinito de "suspend enter"** após `EVT_APP_START`, re-registrando o timer de game loop (16 ms) a cada frame, sem nunca avançar para o desenho real.

---

## 2. video_varied e video_xor

Ambos são **0** (zero). O framebuffer da VRAM emulada (`0x30000000`, 640×480 XRGB8888) permanece todo preenchido com zeros durante toda a execução. Nenhum código de desenho (IDisplay nem GL) escreve pixels nele.

---

## 3. Todos os CreateInstance — CLSIDs usados pelo Supbtime

Extraídos do log de execução (`supbtime_run.log`) e cruzados com `src/brew/aee_ids.h`:

| # | CLSID | Nome (aee_ids.h) | Interface | Onde é criado (linha do log) |
|---|-------|------------------|-----------|------------------------------|
| 1 | `0x010924E3` | Applet do próprio jogo (Super BurgerTime) | IModule → applet | Linha 36 (via `IModule_CreateInstance`) |
| 2 | `0x01001001` | `AEECLSID_DISPLAY_REAL` | DISPLAY / IDisplay | Linha 46-53 (`real_create_instance`) |
| 3 | `0x01001003` | `AEECLSID_FILEMGR_REAL` | FileMgr | Linha 94-96 |
| 4 | `0x0103D8EC` | `AEECLSID_QEGL_REAL` | QEGL (Qualcomm EGL) | Linha 118-121 |
| 5 | `0x010127D4` | `AEECLSID_DISPLAY1_REAL` | DISPLAY1 | Linha 129-132 |
| 6 | `0x0106C411` | `AEECLSID_HID_REAL` | HID (input) | Linha 301-304 |
| 7 | `0x01041207` | `AEECLSID_SIGNALCBFACT_R` | SignalCBFactory | Linha 307-310 |
| 8 | `0x01001017` | `AEECLSID_THREAD_REAL` | Thread | Linha 338-341 |

Não houve `CreateInstance` para `AEECLSID_IMAGE` (IImage), nem para `AEECLSID_GL_REAL` (`0x01014BC3`) como CLSID separado.

---

## 4. DISPLAY, GRAPHICS, EGL, GL, IImage — o que Supbtime usa

| Recurso | Usado? | Detalhes |
|---------|--------|----------|
| **DISPLAY** (`0x01001001`) | **SIM** | `IDisplay` criado em `0x10001528`, armazenado em `applet+0x68` (linha 73 do log: `applet+0x68 = DISPLAY 0x10001528`). `IDisplay_GetDeviceBitmap` (slot 16) é chamado várias vezes (linhas 111-113, 134, 149). |
| **GRAPHICS** | Parcial | `0x01001001` serve como DISPLAY/GRAPHICS; `0x010127D4` (`DISPLAY1_REAL`) também é criado (linha 129). Nenhum método de desenho GRAPHICS é efetivamente invocado. |
| **EGL / QEGL** (`0x0103D8EC`) | **SIM** | Sequência completa de inicialização ESL: `IQEGL_InitGLSurface` (slot 3/4), `IQEGL_Initialize` (slot 5), `IQEGL_ChooseConfig` (slot 9), `IQEGL_slot11`/CreateGL (slot 11), `IQEGL_slot16`/CreateSurface (slot 16), `IQEGL_slot18` (slot 18). `eglGetProcAddress` retorna interfaces V2 (EGLSurfaceManip, GLESImageonExt). |
| **GL** | Indireto | Não há `CreateInstance(0x01014BC3)` (GL_REAL). O GL vem via QEGL `QueryInterface` para IIDs `0x0103D8DD`/`0x0103D8EA` (GLES), `0x0103D8DE`/`0x0103D8EB` (extensões), `0x01051834`/`0x01058546` (surface manipulation). `glGetString(GL_EXTENSIONS, 0x1F03)` e `glEnable(0x0DE1 = GL_TEXTURE_2D)` são chamados. |
| **IImage** | **NÃO** | Nenhum `CreateInstance` para CLSID de imagem foi observado no log. |

---

## 5. Valor de `applet+0x6C`

`applet+0x6C` é **sempre `0x00000000`** durante toda a execução (log, linhas 368, 379, 389, etc., repetido a cada frame):

```
boot: applet=0x100014E8 vtbl=0x10001508 HandleEvent=0x0000B5B4 obj+0x18=0x0001BD88 obj+0x6C=0x00000000
```

**Significado:** `applet+0x6C` é o campo de ponteiro de função de renderização do game loop. Em `src/brew/boot.c`:

- Linhas 1057-1062: para **RE4** (`clsid == 0x0108AF6C`), o código injeta `0x00002F440` em `applet+0x6C` (dispatcher do game loop RE4 que faz `LDR R1,[R0,#0x6C]; BLX R1`).
- Linhas 1366-1391: em `zboot_process_timers`, o `render_fn` de `applet+0x6C` é chamado após o timer **apenas para RE4 e Zeeboids** (`0x0108FF1A`).
- Para todos os outros jogos (incluindo Supbtime), `applet+0x6C` fica limpo (`0`).

Como Supbtime não recebe essa injeção, `applet+0x6C = 0` e nenhum callback de render é disparado após o timer.

---

## 6. Supbtime chama operações de IDisplay?

Sim, mas **apenas operações de setup/consulta**, nunca de desenho efetivo. Do log:

| Operação | Slot | Observações |
|----------|------|-------------|
| `IDisplay_GetDeviceBitmap` | 16 | Chamado 3+ vezes (linhas 111-113, 134, 149) — retorna bitmap do dispositivo `0x1002D798` |
| QEGL `InitGLSurface` | 3 / 4 | Cria superfície EGL sobre o bitmap do device |
| QEGL `Initialize` | 5 | Inicializa EGL |
| QEGL `ChooseConfig` | 9 | Escolhe config EGL |
| QEGL `CreateGL` | 11 | Cria interface GL |
| QEGL `CreateSurface` | 16 / 18 | Cria superfície EGL |
| QEGL `QueryString` | 21 | Consulta strings EGL/GL |
| `glGetString` | — | `pname=0x1F03` (GL_EXTENSIONS) |
| `glEnable` | — | `0x0DE1` = `GL_TEXTURE_2D` |
| QEGL `QueryInterface` | 2 | Múltiplos IIDs: `0x0103D8DD`, `0x0103D8EA`, `0x010426E3`, `0x0103D8EF`, `0x0103D8F0`, `0x01051834`, `0x0103D8DE`, `0x0103D8EB`, `0x0103DEF1`, `0x01058546` |

**Não há nenhuma chamada de desenho efetivo:** nem `IDisplay_DrawText`, `IDisplay_DrawRect`, `IDisplay_BitBlt`, `IDisplay_ClearScreen`, `IDisplay_Update`, nem `glDrawArrays`, `glDrawElements`, `glTexImage2D`. O jogo monta todo o pipeline EGL/GL e depois fica parado.

---

## 7. Ativos que Supbtime carrega do disco

Diretório: `tests/assets/super_burgertime/mod/279125/`

| Arquivo | Tipo | Status no log |
|---------|------|---------------|
| `font.FNZ` | Fonte | Jogo tenta abrir **`font.fnz`** (minúsculo, linha 227) → **NÃO ENCONTRADO** no VFS (linha 230: `IFileMgr_OpenFile: 'font.fnz' (modo 0x1) nao encontrado`) |
| `menu.fnz` | Fonte | Presente no disco |
| `menusmall.fnz` | Fonte | Presente no disco |
| `Dark.tex` | Textura | Presente no disco |
| `gmode.tex` | Textura | Presente no disco |
| `MenuBox1.tex` | Textura | Presente no disco |
| `ding.wav` | Áudio | Presente no disco |
| `supbtime.eng` / `.esp` / `.por` | Strings de idioma | Presentes no disco |
| `supbtime.mod` / `.pkg` / `.sig` | Módulo/pacote/assinatura | Carregado (2.8 MB, entry `0x00000014`) |

**Problema crítico:** O jogo tenta abrir `font.fnz` (minúsculo) mas o arquivo no disco é `font.FNZ` (maiúsculo). O VFS (`data.vfs`, 448 membros, linha 229 do log) não contém `font.fnz`. Após a falha de abertura da fonte, o jogo tenta descomprimir dados e falha (linha 260: `HLE uncompress FAIL`). Isso pode ser o motivo pelo qual o jogo entra em "suspend enter" — está aguardando um recurso que não carrega.

---

## 8. Mecanismo de renderização esperado e o que precisa ser implementado

### Mecanismo esperado

Supbtime é um jogo **2D que usa EGL/GL (QEGL)** para renderização, não IDisplay primitivos. O fluxo esperado é:

1. Criar `IDisplay` (`0x01001001`) e obter o device bitmap via `GetDeviceBitmap`.
2. Criar `QEGL` (`0x0103D8EC`), inicializar EGL, criar superfície EGL sobre o bitmap do device.
3. Obter interface GL via `QueryInterface`.
4. Desenhar quadros texturizados (sprites de hambúrguer, ingredientes, cenário) via `glDrawArrays` / `glDrawElements` com texturas carregadas dos arquivos `.tex`.
5. Chamar `eglSwapBuffers` (ou `IDisplay_Update`) para apresentar o frame.

### Causa raiz da tela preta

1. **Loop de suspend:** Após `EVT_APP_START`, o HandleEvent do applet (`0x0000B5B4`) imprime `suspend enter` a cada frame e nunca transica para o estado de execução normal de renderização.
2. **`applet+0x6C = 0`:** Nenhum callback de render é injetado (a injeção só acontece para RE4 e Zeeboids em `boot.c`). O timer de game loop dispara, chama `HandleEvent`, que imprime "suspend enter" e re-registra o timer — nada desenha.
3. **`font.fnz` não encontrado:** Diferença de maiúsculas/minúsculas entre o que o jogo pede (`font.fnz`) e o que existe no disco (`font.FNZ`). A falha em carregar a fonte impede o avanço do jogo.
4. **Falha de descompressão:** `HLE uncompress FAIL` (linha 260 do log) após a falha da fonte.
5. **Fallback de vídeo ineficaz:** Em `libretro_core.c` linhas 534-544, como `zegl_has_context()` retorna true (EGL foi inicializado), o core chama `glClear` (slot 35) + `eglSwapBuffers` (slot 26). `glClear` apenas limpa o framebuffer com `g_gl_clear_color = 0xFF000000` (preto). Não há conteúdo de jogo para apresentar.

### O que precisa ser implementado

Para tirar Supbtime da tela preta, são necessárias (por ordem de prioridade):

1. **Corrigir a abertura de `font.fnz`:** Fazer o VFS / `IFileMgr_OpenFile` (em `src/brew/ifile.c`) aceitar correspondência case-insensitive, ou mapear `font.fnz` → `font.FNZ`. Isso pode permitir que o jogo saia do estado de suspend.

2. **Injetar `render_fn` em `applet+0x6C` para Supbtime:** Em `src/brew/boot.c`, estender a injeção que hoje só ocorre para RE4 (`0x0108AF6C`) e Zeeboids (`0x0108FF1A`) para também incluir Supbtime (`0x010924E3`). O endereço da função de render do game loop de Supbtime precisa ser descoberto via desassembly (o jogo provavelmente faz `LDR R1,[R0,#0x6C]; BLX R1` como RE4).

3. **Implementar o caminho de desenho EGL/GL:** Como Supbtime espera renderizar via QEGL/GL, o rasterizador de software GLES 1.x já existente em `src/gpu/egl_gl.c` (funções `glDrawArrays`, `glDrawElements`, `glTexImage2D`, `glEnable(GL_TEXTURE_2D)`, etc.) precisa ser ativado/verificado para que as texturas `.tex` sejam rasterizadas no framebuffer. Atualmente apenas `glClear` está implementado entre as funções GL usadas.

4. **Verificar chamadas IDisplay de desenho:** Caso o jogo também use `IDisplay_DrawText` / `IDisplay_DrawRect` / `IDisplay_BitBlt` para UI (menus, HUD), garantir que esses handlers em `src/brew/boot.c` (slots 4, 5, 6 do stub) realmente escrevam no framebuffer e marquem o frame via `zbrew_mark_frame()`.

5. **Garantir `zbrew_mark_frame()` no caminho de apresentação:** Depois que o jogo desenhar (via GL ou IDisplay), o frame precisa ser marcado para que `libretro_core.c` envie o framebuffer para o frontend via `video_cb`.

---

## Arquivos de referência

- `src/brew/boot.c` — ciclo de vida do applet, `real_create_instance`, injeção de `applet+0x6C` (apenas RE4/Zeeboids)
- `src/brew/ishell.c` — `CreateInstance` via IShell (slots 2 e 5)
- `src/brew/idisplay.c` / `idisplay_real.c` — handlers IDisplay (legado e real)
- `src/gpu/egl_gl.c` — stub EGL/QEGL + rasterizador GLES 1.x
- `src/gpu/framebuffer.c` — framebuffer VRAM (640×480 XRGB8888)
- `src/core/libretro_core.c` — `retro_run`, fallback de vídeo (linhas 530-544)
- `src/brew/aee_ids.h` — CLSIDs (Supbtime = `0x010924E3`, linha 129)
- `tests/assets/super_burgertime/mod/279125/` — ativos do jogo (font.FNZ, .tex, .wav, etc.)
- `supbtime_run.log` — log completo da execução de smoke test
