# STATUS B - Render Family Pack

Resumo curto:
- Corrigi a validação de ponteiros de vértice em `src/gpu/egl_gl.c` para respeitar o mapa real de memória do Zeebo.
- O Family Pack agora passa por `glVertexPointer` usando enderecos heap validos como `0x10011CF4` e `0x10011D24`.
- O teste de fumaça confirma renderizacao ativa com `DrawArrays` e frame produzido.

O que mudei:
- `decode_vertex_ptr()` agora aceita RAM, heap, stack e VRAM.
- `glVertexPointer` deixou de marcar heap valido como invalido.
- Mantive o fallback para `stack[0]` apenas quando o ponteiro de entrada realmente aponta para um caso curto/indefinido.

Evidencia do teste:
- `glTexCoordPointer(size=2 type=0x140C stride=0 ptr=0x10011CD4)`
- `glVertexPointer(size=3 type=0x140C stride=0 ptr=0x10011CF4)`
- `glVertexPointer(size=3 type=0x140C stride=0 ptr=0x10011D24)`
- `DrawArrays[0]: mode=0x6 count=4 va_pos.on=1 va_pos.addr=0x10011CF4`
- `DrawArrays[1]: mode=0x6 count=4 va_pos.on=1 va_pos.addr=0x10011D24`
- `frame 121: boot=timer instrucoes=58980218 PC=0x0009028C halted=0 fb[0]=0xFFFFFFFF`

Resultado:
- O jogo continua rodando e desenhando.
- O bug de rejeitar ponteiros heap reais foi removido.
- O frame ainda parece depender de mais ajustes de conteúdo/recursos, mas o bloqueio principal do ponteiro de vértice foi corrigido.
