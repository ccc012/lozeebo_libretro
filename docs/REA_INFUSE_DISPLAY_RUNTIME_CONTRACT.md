# REA/Infuse — contrato comportamental do runtime Display

> Clean-room: comportamento observável e invariantes; nenhum código do Infuse foi copiado.
> Alvo: `re_infuse/bin/Infuse.exe` (18/05/2024). Ferramentas: REA 6.8.0 e projeto Ghidra 12.1.4 `re_infuse/ghidra_project/InfuseRE`.
> Data: 2026-11-06.

## Escolha de ROI

**Display runtime**, não Alice pós-HID.

Motivo: Alice está isolada no caminho TTDMM/softfloat até `IMedia_Play`; Display atende o runtime compartilhado dos ports Data East e também reduz incerteza em RE4/Zuma. Evidência existente já identifica a fronteira completa: guest BREW/Dynarmic → fila de eventos → renderer host OpenGL.

## Evidência e limites

- `NativeInfuseHAL` vtable: `0x1405b7560`, 78 slots; superfície HAL/guest, não uma vtable BREW `IDisplay`.
- `OpenGL::Renderer` vtable: `0x1405b85c0`.
- Eventos RTTI confirmados: Clear, Viewport, DrawText, DrawImage, DrawTriangles, CreateTexture, UpdateTexture, DeleteTexture.
- `RendererEventDrawImage`: vtable `0x1405b84d0`, primeira função `0x140049c10`.
- `RendererEventDrawTriangles`: vtable `0x1405b8560`, primeira função `0x140049860`.
- `RendererEventDrawText`: vtable `0x1405b8470`, primeira função `0x14004a450`.
- O scan estático não encontra RTTI/string de `IDisplay`, `IGraphics` ou `IQEGL`, nem CLSIDs BREW relevantes no host. Logo, não é válido inferir slots BREW a partir das vtables C++ host.
- REA 6.8.0 confirmou o artefato, mas o provider recusou decompilação porque `GHIDRA_INSTALL_DIR` não está configurado. Os endereços e tipos acima vêm do projeto Ghidra 12.1.4 já analisado e dos artefatos versionados. Falha registrada em `C:\Users\Lucas\AppData\Local\Temp\opencode\rea-runtime\`.

## Contrato comportamental

### 1. Fronteira guest/host

1. Uma chamada BREW guest altera estado lógico ou produz uma operação de desenho.
2. A camada HAL traduz a operação para um evento tipado do renderer.
3. O renderer host consome eventos; o guest não chama OpenGL host diretamente.
4. Eventos mantêm ordem de submissão. Não coalescer operações que alterem textura, viewport ou destino entre draws.

**Invariante:** ponteiros guest nunca são tratados como ponteiros host. Dados usados depois do retorno precisam de snapshot/cópia ou handle com vida útil explícita.

### 2. Estado Display

Estado mínimo por contexto:

- destino atual: device ou bitmap offscreen;
- clip/viewport atual;
- cor/fonte/modo de desenho;
- texturas vivas por handle;
- frame dirty/presentável.

`SetDestination(NULL)` seleciona device. Desenho offscreen não apresenta sozinho. Trocar destino não apaga conteúdo nem reseta clip/cor implicitamente.

### 3. Texturas

- `CreateTexture` cria handle host antes do primeiro uso.
- `UpdateTexture` observa dimensões, pitch e formato do bitmap no momento da submissão.
- `DeleteTexture` invalida o handle somente depois dos draws anteriores na fila.
- Draw com handle inexistente/inválido falha de modo determinístico; não reutiliza textura anterior.

**Invariante de formato:** depth, color scheme e pitch são campos independentes. RGB565 usa expansão explícita; nunca assumir 4 bytes por pixel.

### 4. DrawImage / DrawText / DrawTriangles

- `DrawImage`: lê região fonte, destino, clip e estado de blend vigentes na submissão.
- `DrawText`: usa fonte/cor/clip vigentes; texto não deve causar present implícito.
- `DrawTriangles`: usa viewport, textura e estado de raster vigentes; submissão preserva ordem relativa a updates de textura.
- Coordenadas fora do destino são recortadas; não podem escrever fora do buffer.
- Operação vazia ou totalmente recortada é no-op bem-sucedido.

### 5. Frame/presentação

- Draws apenas marcam o frame dirty.
- `IDisplay_Update` ou `SwapBuffers` é a barreira explícita de apresentação, conforme a interface usada pelo jogo.
- Present não limpa framebuffer, destino offscreen, texturas ou estado de desenho.
- Um present sem alterações pode ser no-op; não fabricar pixels para satisfazer `video_varied`.
- QEGL usa `SwapBuffers` slot 25; IEGL usa slot 26. Não unificar layouts deslocando slots.

## Sequência esperada — Data East/Display

1. `CreateInstance(DISPLAY)`.
2. Seleção do device ou criação de bitmap offscreen.
3. Configuração de clip/cor/destino.
4. Desenho 2D (`DrawImage`, `DrawText`, retângulos) e/ou upload de textura.
5. Composição/blit no device.
6. `Update`/`SwapBuffers` apresenta o frame, sem clear destrutivo.

Uma execução que só chega a CreateInstance, DrawArrays ou `video_varied=1` não prova cena correta nem jogabilidade.

## Critérios de implementação e teste

1. Logar sequência monotônica: destino, create/update/delete texture, draw, present.
2. Assertar ausência de draw com textura morta e escrita além de pitch/altura.
3. Teste mínimo: bitmap RGB565 offscreen → draw/blit no device → present; validar pixels conhecidos e bordas de clip.
4. Teste de ordenação: create → update A → draw → update B → draw → delete; primeiro draw usa A, segundo usa B.
5. Smoke Data East: registrar menu/cena reconhecível e input manual. Boot/pixels variados isolados não promovem status.

## Não implementar por inferência

- Não copiar handlers/decompilação Infuse.
- Não mapear vtable `NativeInfuseHAL` diretamente para slots `IDisplay`.
- Não adicionar autopresent, paint sólido ou clear sintético.
- Não alterar `src/` antes de um trace apontar qual invariante acima está quebrado.

## Próximo probe de maior valor

Configurar `GHIDRA_INSTALL_DIR` para Ghidra 12.1.4 e usar REA `function` nos três handlers de evento acima. Extrair somente forma de payload, ownership e ordem de consumo; comparar com trace Data East. Não transportar expressões ou código do Infuse.
