# Diagnóstico de Progresso: Bejeweled Twist

## Causa Comprovada

Bejeweled Twist (`bjt.mod`) não renderiza gameplay nem menu interativo por incompatibilidade arquitetural em cadeia na API gráfica e runtime de applet:

1. **Pipeline de Render (Bitmap vs Display)**:
   - O jogo decodifica fontes/sprites via `IImage` e direciona as operações para instâncias de `IBitmap` guest alocadas internamente (`IDisplay_SetDestination` recebe wrappers guest `+0=vtbl`, não `IDIB` nativo).
   - O núcleo LOZeebo atual desenha `IImage_Draw` diretamente no framebuffer do dispositivo (`zfb_pixels()`), ignorando se o destino selecionado via `IDisplay_SetDestination` é um bitmap intermediário ou se o applet solicitou desenho offscreen.
   - Consequentemente, o atlas de glifos (`743` e `740` bytes) e fontes descompactadas durante a carga são carimbados no canto inferior e topo da tela física como sujeira visual estática, sem que o compositor interno do jogo consiga projetar os elementos na cena.

2. **Bloqueio de Runtime e Eventos**:
   - `0x0100100F` é consultado via `real_create_instance`. Embora receba stub transparente (`slot 5`), o jogo sequer depende de seu retorno para travar a tela: ele despacha `EVT_APP_START`, programa timer de 16 ms e aguarda ciclos de decodificação assíncrona (`zbrew_queue_signal_callback`).
   - Mesmo estendendo a execução para 600 frames com injeção de inputs repetidos (`A:120,A:240,START:360,A:480`), o frame de vídeo permanece preso no mesmo atlas recortado. Não há transição para render do tabuleiro nem geração de áudio (`audio_nonzero=0`).

## Provas Visuais Antes/Depois

- **Antes (triagem 180f)**: `artifacts/capability-gate/.../Bejeweled Twist/frame.ppm` (atlas de caracteres azuis e gemas cortadas na porção inferior e canto superior esquerdo).
- **Depois (teste estendido 600f com input)**: Mesmo atlas estático persistente, provando ausência de menu real ou loop interativo.

## Conclusão e Handoff

- **Status**: Diagnóstico honesto. Não foi aplicado hack cosmético (rejeitado fake `HandleEvent` ou supressão arbitrária de alpha).
- **Ação necessária para destravar**:
  1. Suporte completo a composição em bitmaps intermediários offscreen em `src/brew/iimage.c` e `src/brew/idisplay_real.c`.
  2. Implementação do subsistema de renderização indireta onde `IImage_Draw` respeita o buffer de destino do `IDisplay`.
