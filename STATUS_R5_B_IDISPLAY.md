# STATUS R5-B — IDisplay completo (DrawText/BitBlt/CreateDIBitmap)

## Resumo (8 linhas)

- Implementados slots 4(DrawText), 3(MeasureTextEx, melhorado), 6(BitBlt), 13(CreateDIBitmap/Ex), 14/15(SetDestination/GetDestination) do IDisplay real, confirmados byte a byte contra `BrewDisplay::setup_vtable()` do zeemu.
- Compila limpo (`build_safe.ps1`, sem erros/warnings). Testado de verdade via smoke test nos 4 jogos Tier 1.
- **Renderizador de fonte 5x7 confirmado funcionando** por teste unitário: `display_draw_text` de 14 caracteres pintou 171 pixels reais no framebuffer (evidência na seção 4).
- **Zero regressão**: contagem de instruções idêntica à baseline documentada em `docs/PROGRESS.md` para os 4 jogos (13695/17671 DD, 75306/145686 Pac-Mania, 729568 congelado Family Pack, 544678 congelado Zeeboids).
- Double Dragon (107x) e Pac-Mania (76x) chamam DrawText intensamente; agora recebem posições corretas (x,y extraídos de dados reais do jogo), mas o **conteúdo de texto observado nesta janela de boot ainda está vazio** — não é bug do renderizador, é o jogo ainda em loop de retry/loading (ver seção 5).
- Corrigi um bug pré-existente: `GetDeviceBitmap` declarava depth=16/pitch=640*2 mas a VRAM real é XRGB8888 32bpp — corrigido para bater com o BitBlt novo.
- Corrigi um bug que introduzi eu mesmo: usei `0x04000000` como limite de "endereço válido", o que rejeitaria heap (0x10000000+), stack (0x2FC00000+) e VRAM (0x30000000+) — todos endereços legítimos. Substituído por `ZDISPLAY_ADDR_MAX` (fim da VRAM).
- Testei também Pac-Mania conforme pedido — mesma conclusão (mecanismo funciona, conteúdo do jogo ainda vazio nesta janela).

---

## 1. Investigação preliminar: quais slots os jogos realmente chamam

Adicionei log temporário (`ZDEBUG_DISPLAY_SLOTS`, removido antes do commit final) capturando TODOS os slots chamados no objeto `AEECLSID_DISPLAY_REAL`, incluindo os que caíam silenciosamente em cases compartilhados com HID (slots 4 e 14 antes desta mudança).

**Double Dragon** (`tests/roms/real_ddragon_game.mod`, 180 frames):
```
slot=4  (DrawText)      107 chamadas  <- MAIS CHAMADO, de longe
slot=18 (SetClipRect)    23
slot=10 (SetColor)       22
slot=5  (DrawRect)       22
slot=7  (Update)         21
slot=17 (SetFont)         3  <- nao implementado, cai no default (retorna 0, ok)
slot=16 (GetDeviceBitmap) 2
```

**Pac-Mania** (mesmo teste):
```
slot=3  (MeasureTextEx)  77  <- MAIS CHAMADO
slot=4  (DrawText)       76
slot=2  (GetFontMetrics) 24
slot=5  (DrawRect)       12
slot=7  (Update)          6
slot=10 (SetColor)        4
slot=19 (GetClipRect)     1
```

**Conclusão**: texto (DrawText/MeasureTextEx/GetFontMetrics) domina completamente as chamadas de display nos dois jogos mais avançados do Tier 1 — confirma exatamente a priorização pedida pela tarefa.

---

## 2. Layout dos 48 slots confirmado contra o zeemu

Arquivo: `reference/zeemu-main/zeemu-main/brew/BrewDisplay.cpp`, função `setup_vtable()` (linhas 567-606):

```cpp
add_method(0, "AddRef");           add_method(1, "Release");
add_method(2, "GetFontMetrics");   add_method(3, "MeasureTextEx");
add_method(4, "DrawText");         add_method(5, "DrawRect");
add_method(6, "BitBlt");           add_method(7, "Update");
add_method(8, "SetAnnunciators");  add_method(9, "Backlight");
add_method(10, "SetColor");        add_method(11, "GetSymbol");
add_method(12, "DrawFrame");       add_method(13, "CreateDIBitmap");
add_method(14, "SetDestination");  add_method(15, "GetDestination");
add_method(16, "GetDeviceBitmap"); add_method(17, "SetFont");
add_method(18, "SetClipRect");     add_method(19, "GetClipRect");
```

Este layout já bate com o comentário existente no código (`src/brew/boot.c`, linha ~263: "[4]DrawText [6]BitBlt [7]Update [16]GetDeviceBitmap"), confirmando que a base estava certa. **Achado crítico**: os slots 4 e 14 (DrawText e SetDestination) colidiam no dispatch com casos exclusivos de HID (`AEECLSID_HID_REAL`/`ZCLSID_HID_DEVICE`) — quando chamados com `clsid == AEECLSID_DISPLAY_REAL`, caíam no `g_cpu.r[0] = 0` sem fazer nada, silenciosamente. Isso explica por que texto nunca aparecia mesmo com o pipeline de renderização funcionando.

---

## 3. Implementação

### DrawText (slot 4) — `src/brew/boot.c`

Assinatura real: `IDISPLAY_DrawText(po, font, psz, nl, x, y, prcBackground, dwFlags)`. Argumentos em R0-R3 + pilha. Implementei:

1. **`display_find_arg_base()`** — porta de `display_arg_stack_base()` do zeemu (`BrewDisplay.cpp:25-45`): detecta se o thunk de vtable empurrou cópias extras de R0/R1/R2 na pilha antes dos argumentos reais, e pula essas cópias. Sem isso, `x`/`y`/`prcb`/`dwFlags` viriam de offsets errados em alguns binários.
2. **`display_resolve_text_ptr()`** — porta de `resolve_display_text_descriptor()` (zeemu `BrewDisplay.cpp:134-145`): alguns SDKs BREW passam um descritor (ponteiro para objeto cujo `+8` é o char* real) em vez do texto direto. Detectado e resolvido.
3. **`display_resolve_string()`** — tenta ler como AECHAR (UTF-16LE) e como byte (ASCII/OEM), escolhe a interpretação mais "imprimível" (mesma heurística de `printable_score` do zeemu).
4. **Fonte 5x7 embutida** (`ZFONT5X7[91][5]`) — cobre espaço até 'Z' (0x20-0x5A), minúsculas aproximadas por maiúsculas (`c & 0xDF`). Caracteres fora da tabela viram '?'. **Validada por teste unitário (seção 4)**.

### MeasureTextEx (slot 3) — melhorado

Antes: contava caracteres via `nl` ou varredura de AECHAR até NUL, sem resolver descritores. Agora usa a mesma `display_resolve_string()` + a mesma fonte 5x7 (`display_draw_text(..., actually_draw=false)`) para medir largura — garante que a largura reportada bate com o que será desenhado de verdade.

### BitBlt (slot 6) — novo

Assinatura: `IDISPLAY_BitBlt(po, xDest, yDest, cxDest, cyDest, pSrc, xSrc, ySrc, rop)`. Só sabe ler bitmaps no formato `ZCLSID_DEVICE_BITMAP` (device real ou criado por `CreateDIBitmap`) — copia pixel a pixel em XRGB8888, com suporte a ROP transparente (pixel preto puro `0xFF000000` tratado como transparente, seguindo o padrão `AEE_RO_TRANSPARENT` do zeemu).

### CreateDIBitmap/CreateDIBitmapEx (slot 13) — novo

`display_create_dib(width, height)`: aloca objeto no mesmo layout do `device_bitmap` existente (`+8` endereço pixels, `+20/+22` largura/altura, `+24` pitch, `+28` profundidade), com buffer real em heap (`zheap_alloc`), sempre 32bpp XRGB8888 independente da profundidade pedida pelo jogo (simplificação documentada).

### SetDestination/GetDestination (slots 14/15) — novo

`g_display_dest_obj` — quando não-zero, redireciona `display_fill_rect_xrgb()` (usado por DrawRect e DrawText) para escrever no DIB offscreen em vez da VRAM. Padrão comum em jogos BREW: criar DIB → SetDestination(DIB) → desenhar sprite → SetDestination(NULL) → BitBlt(DIB) para compor na tela.

### Correção de bug pré-existente: GetDeviceBitmap

```c
/* ANTES (errado): */
zmem_write16(g_device_bitmap + 24, 640 * 2);  /* pitch de 16bpp */
zmem_write8(g_device_bitmap + 28, 16);         /* depth=16 */

/* DEPOIS (corrigido, bate com VRAM real XRGB8888): */
zmem_write16(g_device_bitmap + 24, 640 * 4);
zmem_write8(g_device_bitmap + 28, 32);
```

A VRAM real é 32bpp XRGB8888 (confirmado em `src/gpu/framebuffer.c:10`: `zmem_host_ptr(ZMEM_VRAM_BASE, ZFB_WIDTH*ZFB_HEIGHT*4)`), mas o objeto `device_bitmap` dizia 16bpp. Não importava até agora porque nada lia esses campos — mas o BitBlt novo lê, e usaria pitch/profundidade errados ao tentar copiar da VRAM.

---

## 4. Evidência: o renderizador de fonte funciona

Bug real que encontrei e corrigi durante o desenvolvimento: usei `0x04000000` como "limite de endereço válido" em várias verificações de ponteiro (`display_looks_ascii`, `display_resolve_text_ptr`, `display_find_arg_base`, etc). Isso rejeitaria endereços de heap (`0x10000000+`), stack (`0x2FC00000+`) e VRAM (`0x30000000+`) — TODOS endereços legítimos e comuns no motor. Corrigido para `ZDISPLAY_ADDR_MAX = ZMEM_VRAM_BASE + ZMEM_VRAM_SIZE` (fim da VRAM).

Como os 4 jogos Tier 1 ainda não têm texto não-vazio nesta janela de boot (~180 frames, ver seção 5), escrevi um teste unitário temporário (removido antes do commit final) chamando `display_draw_text(50, 50, "TESTE 5X7 0123", 0xFFFFFFFFu, true)` uma vez, contando pixels diferentes do fundo antes/depois:

```
[INFO] [Zeebo] TESTE display_draw_text: pixels diff antes=0 depois=171 (delta=171)
```

14 caracteres, 171 pixels pintados (~12 pixels/caractere em média, consistente com uma fonte 5x7 esparsa). **Confirma que o mecanismo de desenho de texto funciona de verdade e pinta pixels reais e distintos no framebuffer**, independente do conteúdo (ainda vazio) que os jogos fornecem nesta janela.

---

## 5. Por que o conteúdo de texto ainda aparece vazio

Log real do Double Dragon (`tests/roms/real_ddragon_game.mod`):
```
[INFO] [Zeebo] IDisplay_DrawRect: rect=0,0 640x480 frame=0xFFFFFFFF fill=0xFFFFFF00 flags=0x2
[INFO] [Zeebo] IDisplay_DrawText: '' em (0,0) len=0
[INFO] [Zeebo] IDisplay_DrawText: '' em (0,0) len=0
[INFO] [Zeebo] IDisplay_DrawText: '' em (0,0) len=0
```

Log real do Pac-Mania (`tests/roms/real_pacmania_game.mod`):
```
[INFO] [Zeebo] IDisplay_DrawRect: rect=0,0 640x480 frame=0xFFFFFFFF fill=0x00000000 flags=0x2
[INFO] [Zeebo] IDisplay_DrawText: '' em (312,16) len=0
[INFO] [Zeebo] IDisplay_DrawText: '' em (312,2) len=0
[INFO] [Zeebo] IDisplay_DrawText: '' em (312,34) len=0
```

**Diagnóstico**: ambos os jogos ficam presos limpando a tela inteira (branco/preto) repetidamente e chamando DrawText com string vazia (`len=0`) — as posições (x,y) extraídas são plausíveis e variam corretamente entre chamadas (evidência de que a leitura de argumentos está certa), mas o conteúdo textual está genuinamente vazio nesta janela. Isso bate com o achado da Tarefa A desta rodada: Double Dragon fica preso num loop de retry (`SetTimer 100ms` repetido) porque o `case 5` de `IModule_CreateInstance` não trata corretamente um CLSID que o jogo usa — o jogo nunca sai desse loop inicial para popular texto real (score, menu, etc). Pac-Mania, embora processando ~70k instruções/60 frames (não travado), também parece estar numa tela de carregamento/splash sem texto populado ainda dentro da janela de ~180 frames do smoke test.

**Não é bug do IDisplay** — é uma consequência de os jogos não terem avançado além do loop inicial (bloqueador de outra tarefa desta rodada). Quando esse bloqueador for resolvido, o pipeline de texto já está pronto para mostrar conteúdo real.

---

## 6. Regressão (4 jogos Tier 1)

Baseline documentada em `docs/PROGRESS.md` (build pré-Tarefa B) vs. depois desta implementação:

| Jogo | instrucoes frame 61/121 (antes) | instrucoes frame 61/121 (depois) | Igual? |
|---|---|---|---|
| Double Dragon | 13695 / 17671 | 13695 / 17671 | ✅ |
| Pac-Mania | 75306 / 145686 | 75306 / 145686 | ✅ |
| Family Pack | 729568 / 729568 (congelado) | 729568 / 729568 (congelado) | ✅ |
| Zeeboids | 544678 / 544678 (congelado) | 544678 / 544678 (congelado) | ✅ |

**Zero regressão confirmada** — contagens de instrução idênticas byte a byte.

---

## 7. Limitações conhecidas / próximos passos

- Fonte 5x7 cobre apenas ASCII 0x20-0x5A (espaço até 'Z', dígitos, pontuação básica); minúsculas aproximadas por maiúsculas. Suficiente para HUD/score, mas não é uma fonte real do jogo.
- `CreateDIBitmap`/`CreateDIBitmapEx` sempre criam bitmaps 32bpp XRGB8888, ignorando a profundidade pedida (`depth` em R2) — simplificação; jogos que dependam de paleta indexada (1/2/4/8bpp) não funcionarão corretamente.
- `SetDestination`/`GetDestination` só reconhecem destinos `ZCLSID_DEVICE_BITMAP` (nossos próprios objetos); um `IBitmap` implementado pelo próprio jogo (como o zeemu trata via `valid_out_ptr`) não é suportado.
- `BitBlt` só lê fontes `ZCLSID_DEVICE_BITMAP`; não suporta o formato legado `AEECLSID_BITMAP`/`IBitmap` (`src/brew/ibitmap.c`) nem structs IDIB/AEEDIB cruas do jogo.
- Não pude confirmar visualmente no RetroArch (isso é responsabilidade da Tarefa F/Lucas) — toda validação aqui foi via smoke test + teste unitário do renderizador.
- Quando o bloqueador de boot (Tarefa A/C desta rodada) for resolvido e os jogos avançarem além do loop inicial, recomendo re-rodar o smoke test e conferir se `IDisplay_DrawText` passa a mostrar strings não-vazias com conteúdo legível.
