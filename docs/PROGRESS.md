# Progresso do Projeto

## Semana 1: Estrutura e Skeleton - CONCLUIDO
- [x] Estrutura de pastas, libretro.h, skeleton com 28 funcoes
- [x] Compilacao VS2022, DLL reconhecida pelo RetroArch
- [x] Testes 1 e 2 do TESTE_RETROARCH.md passando

## Fases 1-4 (nucleo funcional) - CONCLUIDO

### Fase 1: CPU ARM - IMPLEMENTADA
- [x] Estrutura da CPU (R0-R15, CPSR, modo User)
- [x] Fetch-decode-execute com deteccao de trap HLE
- [x] Data processing completo (16 opcodes, barrel shifter completo)
- [x] MUL/MLA/UMULL/UMLAL/SMULL/SMLAL, CLZ, SWP
- [x] LDR/STR/LDRB/STRB/LDRH/STRH/LDRSB/LDRSH/LDRD/STRD
- [x] LDM/STM todos os modos, B/BL/BX/BLX com interworking
- [x] Modo Thumb completo (19 formatos + SXTB/SXTH/UXTB/UXTH/REV)
- [x] Flags NZCV corretas (add/sub/logicas/shifts)

### Fase 2: Memoria & Loader - IMPLEMENTADA
- [x] Mapa de memoria HLE (docs 09): RAM 64MB, heap 32MB, stack 4MB, VRAM 2MB
- [x] Read/Write 8/16/32 little-endian, bounds checking que nunca trava
- [x] Heap com alocador de blocos (MALLOC/FREE/REALLOC com merge)
- [x] Loader de binario flat em 0x1000 (parse do header MOD real: requer RE)
- [x] MIF: extracao basica de nome; BAR: deteccao (parse completo: fase 2.3)

### Fase 3: BREW HLE - IMPLEMENTADA (base)
- [x] Sistema de trap por enderecos magicos 0xF0000000+
- [x] Vtables na memoria emulada apontando para traps
- [x] IShell (CreateInstance, GetDeviceInfo, uptime)
- [x] MALLOC/FREE/REALLOC/MEMSET/MEMCPY
- [x] IFile/IFileMgr (mapeado ao diretorio da ROM, com sandbox)
- [x] DBGPRINTF, GetTimeMS, GetKeys
- [ ] IShell_SetTimer + callbacks (fase 3.5)
- [ ] Applet lifecycle completo (AEEMod_Load real: depende de RE do MOD)

### Fase 4: Graficos & Audio - IMPLEMENTADA (base)
- [x] Framebuffer 640x480 XRGB8888 na VRAM emulada
- [x] IDisplay: Update/ClearScreen/SetColor/FillRect/DrawRect/DrawLine/DrawPixel/BitBlt
- [x] IBitmap com transparencia (magenta)
- [x] Mixer 44.1kHz estereo, 16 vozes, resampling, PCM U8/S16
- [x] ISound Play/Stop/SetVolume
- [x] Input RetroPad -> bitmask Zeebo

### Validacao (ROM de teste)
- [x] tests/roms/make_test_rom.py gera test_draw.mod (ARM montado a mao)
- [x] CPU executa 60M instrucoes/s (1M/frame @ 60fps)
- [x] Pipeline completo validado: loader -> CPU -> trap HLE -> framebuffer -> video
- [x] Tela azul + retangulo vermelho confirmados visualmente no RetroArch

## Validacao com ROMs reais (Pac-Mania e Zeebo Family Pack)
- [x] Build validado em `Release|x64` com Visual Studio 2022
- [x] `tests/libretro_smoke.c` roda ROMs reais e captura logs sem depender da interface grafica do RetroArch
- [x] Loader com fallback por nome de caminho para reconhecer Pac-Mania/Double Dragon quando o CLSID nao aparece direto no MIF
- [x] `retro_load_game` com fallback por `path` (quando o frontend nao entrega `info->data`) e extensao `zip` habilitada no `valid_extensions`; extra��o permanece no frontend
- [x] Base de assets ajustada para dumps no layout `mod/<id>/<jogo>.mod` (raiz do pacote passa a ser usada para `IFileMgr`)

**Pac-Mania**: passa por `AEEMod_Load`, resolve o CLSID `0x01087B72` e entra em `IModule_CreateInstance`. A classe BREW `0x0100101C` e criada como stub, com os dois resultados do metodo 5 inicializados. Ficava presa em loop infinito dentro do `CreateInstance` (ver diagnostico e correcao na secao "Bloqueio atual" abaixo).

**Zeebo Family Pack**: CLSID `0x010903C6` extraido do MIF real. O applet passa por `AEEMod_Load`, `IModule_CreateInstance`, inicializacao de display, arquivos, som e joystick:
- `AEEHelper_GetAppInstance` implementado com base no comportamento do Zeemu.
- `SignalCBFactory_CreateSignal` cria e devolve objetos validos nos dois parametros de saida (padrao de referencia para stubs de classe, ver bug do Pac-Mania abaixo).
- `IHID_GetConnectedDevices`, `GetDeviceInfo` e `CreateDevice` expoem um joystick Zeebo valido.
- `IFileMgr` real conectado ao boot; o jogo ja tenta abrir `udata\highscore.dat`.
- `IDisplay_GetDeviceBitmap` devolve um bitmap RGB565 de 640x480 associado a VRAM.
- `CreateInstance` termina com applet valido; `EVT_APP_START=0` e tratado e o estado chega a "rodando" sem descarrilar.
- `AEECLSID_EGL` e `AEECLSID_GL` ja estao em HLE funcional suficiente para bootstrap, consulta de config e desenho minimo 2D/3D; o applet agora produz frames e apresenta o framebuffer via `eglSwapBuffers`.
- Bloqueio atual: o jogo ainda depende de cobertura mais ampla da pilha GLES 1.x para menus e sprites completos, mas o caminho de imagem ja esta vivo.

## Sessao 2026-07-08 (2): Family Pack chega ao loop de render (tela branca via glClear real)

Grande avanco verificado por smoke test contra a ROM real:

1. **Corrupcao do `data.vfs` diagnosticada e corrigida** (`src/brew/ifile.c`): a vtable
   do IFile estava fora de ordem (faltavam `Readable`/`Cancel` do IAStream, entao o
   `IFILE_Read` do jogo caia no trap de *Write* do emulador) e o modo de abertura
   tratava `_OFM_READ` (0x1, so-leitura) como gravavel. Resultado: o "read" de 4 bytes
   do magic gravava 4 zeros no offset 0 do arquivo real. Corrigido: vtables IFile (12
   slots) e IFileMgr (21 slots) na ordem real do AEEFile.h (confirmada no BrewFileMgr
   do zeemu), modo OFM como bitmask, Seek com a ordem real (0=inicio 1=FIM 2=ATUAL -
   invertida vs stdio), GetInfo/Readable/Cancel implementados.
2. **Regressao dos helpers corrigida** (`src/brew/helpers.c`): os cases `wstrlen`,
   `strtowstr` e `wstrtostr` tinham sido removidos num refactor inacabado - religados
   usando as estaticas ja escritas (e corrigido um bug de capacidade/2 na
   `guest_wstrtostr`).
3. **Crash do IDisplay real revertido** (`src/brew/boot.c`): rotear
   `AEECLSID_DISPLAY_REAL` para uma vtable propria de 10 slots em ordem inventada
   fazia o jogo ler alem do fim da vtable ([16]=GetDeviceBitmap no layout real de 48
   slots), saltar para 0x0, re-executar o entry do modulo (zerando o BSS) e
   descarrilar com SP=0. Voltou ao stub generico consciente de clsid; metricas de
   fonte (GetFontMetrics/MeasureTextEx, slots 2/3) agora preenchidas.
4. **Vtable IGL realinhada ao layout real** (`src/gpu/egl_gl.c`): [0-2] COM +
   funcoes GLES 1.x fixed-point em ordem alfabetica a partir do slot 3 (glClear=7,
   glGenTextures=36, glRotatex=63...), confirmado no BrewGL do zeemu; tabela direta
   "sem this" espelhada no corpo do objeto (obj[1+i]); `eglGetConfigAttrib` devolve
   os valores reais do hardware (RGB565: BUFFER_SIZE=16, DEPTH_SIZE=16...);
   `eglSwapBuffers` apresenta em vez de apagar o framebuffer; `glClearColorx` trata
   fixed-point 16.16 corretamente.
5. **Loader endurecido** (`src/loader/mod_loader.c`): magic `PK`/GZIP rejeitado com
   mensagem clara em vez de executar o container como codigo ARM em VA 0.

**Estado atual do Family Pack**: boot completo -> `EVT_APP_START` -> "rodando";
pilha EGL inteira inicializa (GetDisplay/Initialize/ChooseConfig/CreateSurface/
CreateContext/MakeCurrent/QuerySurface); `data.vfs` abre e o jogo streama assets
reais dele (header FUFS + tabela de indice + chunks de 4KB); `glClear` real chega
pela vtable corrigida e pinta o framebuffer de branco (`fb=0xFFFFFFFF`).

**Bloqueio atual**: o jogo estagna num busy-wait dentro do callback de timer - um
loop de divisao por subtracao (0x7AAC8) cujo divisor `[obj+0x54]` e 0. O zero se
propaga de uma cadeia de leituras a partir de um ponteiro de tabela NULL
(`[R5+0x20]`) num objeto do scheduler do jogo: NULL-deref nao falha aqui (endereco
0 e RAM valida com o modulo carregado), entao o guest le bytes de codigo como
"ponteiros" e a politica "leitura invalida -> 0" mantem o loop vivo porem
degenerado. Proximo passo: instrumentar o lookup da tabela do scheduler (PC 0x4A20:
`R0=[R3+R2*4]`, R3=[R5+0x20]) e descobrir qual init de nivel superior falhou
(candidatos: ISound stub 0x01001056, metodos 11-13 do IHIDDevice sem out-params,
callback de ISHELL_Resume nunca disparado). Watchpoints de debug desta cacada estao
em cpu.c/memory.c/ifile.c (temporarios, remover quando o bug fechar).

## Sessao 2026-07-08 (3): RESOLVIDO - Family Pack entra no loop de render de verdade

A cacada do divisor zero fechou. Cadeia completa da causa raiz:

1. O "busy-wait" era o rasterizador de software do proprio jogo dividindo por uma
   coordenada zero. O zero vinha de elementos NULL na tabela do scheduler grafico.
2. Os elementos eram NULL porque a fabrica de superficies do jogo (0x7BC10 via
   0x100CC) falhava: ela depende de chamadas GL (glGenTextures, glGetIntegerv...)
   que chegavam com **argumentos deslocados**.
3. **Causa raiz final: convencao de chamada do wrapper GL da Qualcomm.** As funcoes
   `gl*` da vtable IGL NAO recebem `this` - os argumentos GL comecam direto em R0
   (confirmado no `BrewQXGLDispatch.cpp` do zeemu: `gl_arg(0)=r0`). So AddRef/
   Release/QueryInterface (slots 0-2) seguem COM classico. Nossos handlers liam
   this em R0 e args em R1+ -> `glGenTextures` recebia um ponteiro como contagem
   (269 milhoes), `glGetIntegerv` recebia ponteiro como pname, etc.

Corrigido em `zgl_handle()` (egl_gl.c): slots 3..79 e tabela direta usam ambos
args a partir de R0. Resultado verificado no smoke test:

- Argumentos GL perfeitos: `glGetIntegerv(GL_MAX_TEXTURE_UNITS)`, `glHint`,
  `glClearColorx(1.0,1.0,...)`, `glGenTextures(n=2)`, `glBindTexture(GL_TEXTURE_2D,1)`,
  `glTexImage2D`, `glTexParameterx`, `glBlendFunc`, `glAlphaFuncx`... (23 funcoes
  GL distintas chamadas pelo jogo).
- Os 4 elementos da fabrica de superficies nascem validos (nenhum NULL).
- **Loop de render vivo**: `glClear(COLOR|DEPTH)` -> `glDrawArrays(GL_TRIANGLE_FAN)`
  -> `eglSwapBuffers` repetindo frame apos frame, estado "rodando", sem crash e sem
  stall ate o fim dos 180 frames do smoke.
- O jogo carrega o menu inteiro do data.vfs (avatares, botoes resume/restart/quit,
  logos dos minigames cannonball/zap, highscore...).

**Proximo passo (a peca final para VIDEO de gameplay): rasterizador de software
GLES 1.x minimo** em egl_gl.c - hoje `glDrawArrays`/`glTexImage2D` sao no-ops,
entao a tela fica na cor do glClear (branca). Minimo viavel para o menu 2D do
Family Pack: armazenar texturas de `glTexImage2D`, estado de `glVertexPointer`/
`glTexCoordPointer`/`glEnableClientState`, matrizes fixed-point (LoadIdentity/
Orthox/Translatex/Scalex), e rasterizacao de `GL_TRIANGLE_FAN` texturizado (quads
do menu) escrevendo na VRAM. Referencia de semantica: `process_draw_call` no
`BrewQXGLDispatch.cpp` do zeemu (1945 linhas - portar o subconjunto minimo).
Depois do rasterizador: testar no RetroArch real e gravar o video.

## Proximos Passos (Fase 2.2/5: jogos reais)

### Bloqueio atual - Pac-Mania: SP sai da stack real e entra na VRAM durante o CreateInstance
- **Sintoma original**: durante `IModule_CreateInstance` do Pac-Mania (CLSID `0x0100101C`), o SP do guest crescia 0x58 bytes a cada iteracao ate a CPU terminar em `0xEA00000C`, sem qualquer sinal diagnosticavel do que estava acontecendo.
- **Causa raiz encontrada**: a guarda de trap HLE em `src/cpu/cpu.c` (`if (pc >= ZMEM_HLE_BASE)`) nao tinha limite superior. Qualquer PC corrompido/descarrilado `>= 0xF0000000` era silenciosamente absorvido como uma falsa "chamada de API nao implementada" (logada como aviso inofensivo, retornando `EFAILED` e retomando o guest via `BX LR`) em vez de ser sinalizado como crash. Isso deixava o codigo do Pac-Mania preso num loop infinito dentro do `CreateInstance`, crescendo o SP em 0x58 bytes por iteracao, de forma invisivel.
- **Correcao aplicada e verificada**: adicionado `ZMEM_HLE_END` (`0xF0001000u`) em `src/memory/memory.h`; a guarda do `cpu.c` passou a ser `if (pc >= ZMEM_HLE_BASE && pc < ZMEM_HLE_END)`. Qualquer coisa fora da janela real de trap agora cai na checagem ja existente de `pc_fetchable()`/"CPU descarrilou". Confirmado via rebuild + execucao contra a ROM real do Pac-Mania: o emulador agora para de forma limpa com um trace completo de PC em vez de entrar em loop silencioso para sempre.
- **Nova evidencia de diagnostico (pos-fix)**: com a guarda corrigida, a CPU descarrila de forma limpa em `PC=0xFF000000`, com `LR=0x0000115C` e, principalmente, `SP=0x30000048` no momento do descarrilamento - dentro da faixa de endereco da VRAM (`0x30000000-0x301FFFFF`), e nao da regiao real de stack (`0x2FC00000-0x2FFFFFFF`). A VRAM e pre-preenchida com a palavra literal de 32 bits `0xFF000000` na inicializacao (`framebuffer.c`, `zfb_clear(0xFF000000u)`, cor de limpeza), o que explica por que qualquer leitura de uma "stack pointer" residente em VRAM retorna exatamente `0xFF000000` e acaba sendo usada como destino de branch. O trace mostra o guest saltando `0xF0000C04 -> 0x115C -> 0x1160 -> 0x1164 -> 0xEB4 -> 0xEB8 -> 0xFF000000` antes de descarrilar.
- **Hipotese confirmada e corrigida em `aa3dfe2` (2026-07-10)**: `zbrew_handle_stub()` em `src/brew/boot.c`, no case 5 (CLSID `0x0100101Cu`), escrevia `NULL` nos dois out-params de stack do caller em vez de construir sub-objetos de stub reais - diferente do padrao que ja funcionava no case 3 (`SignalCBFactory`). Corrigido para alocar objetos reais via `make_stub_interface()`, no mesmo padrao do case 3. **Ainda pendente**: re-testar a ROM real do Pac-Mania de ponta a ponta contra o build atual e confirmar que o `CreateInstance` completa sem descarrilar (o fix foi validado por build/smoke parcial, nao por um passe completo confirmado no RetroArch - ver sessao 2026-07-10 (2/3) abaixo).

### Checklist
- [ ] Reverse engineering do formato MOD real (Ghidra) - entry point, relocacao completa
- [ ] Vtables/class IDs reais do BREW (RE do Infuse/documentacao arquivada) - CLSIDs do stub `0x0100101C`, Pac-Mania (`0x01087B72`) e Zeebo Family Pack (`0x010903C6`) ja resolvidos via MIF real; demais titulos ainda pendentes
- [x] IShell_SetTimer + inicio do event loop de applet: `EVT_APP_START=0` tratado, Zeebo Family Pack chega ao estado "rodando"
- [x] Testar com ROM real do Zeebo Family Pack: boot completo ate "rodando" (ver secao de validacao acima)
- [x] Corrigir o desvio do SP para a VRAM durante o `CreateInstance` do Pac-Mania (fix aplicado em `aa3dfe2`; falta reconfirmar com teste real end-to-end)
- [x] Rasterizador GLES 1.x minimo portado e conectado ao framebuffer libretro (`7cc847a`/`b427955`/`aa3dfe2`/`7a55083` - Family Pack ja desenha triangulos texturizados reais, mas coordenadas fora de tela e vertex pointer `0x3` invalido ainda bloqueiam um frame correto, ver sessao 2026-07-10 (2/3))
- [ ] Popular `tests/test_cpu.c` e `tests/test_memory.c` (ainda vazios)
- [ ] Save states (serialize da CPU+memoria)
- [ ] ADPCM/MP3/MIDI (audio completo)

## Sessao 2026-07-10 (2/3): Rasterizador GLES 1.x minimo + vertex pointer via stack

Sequencia de commits que levou o Family Pack do "so glClear branco" a desenhar
formas de verdade:

1. **`7cc847a` - Rasterizador GLES 1.x minimo ativo**: `raster_triangle()`
   implementado em `egl_gl.c` (interpolacao baricentrica de cor), primeiros
   pixels escritos com cores interpoladas na VRAM.
2. **`b427955` - Vertex pointer data flow fixed**: descoberta de que
   `glVertexPointer` as vezes recebe o endereco real dos dados no `stack[0]`
   em vez de num registrador, quando `R3==SP` (convencao de bootstrap ROPI
   observada no binario real). Antes disso o ponteiro lido era lixo
   (`0xCCAFF500`). Resultado confirmado: um quadrilatero colorido com
   gradiente renderizado e verificado visualmente (screenshot).
3. **`aa3dfe2` - Fix Pac-Mania CreateInstance crash + Family Pack rendering**:
   - Pac-Mania: `zbrew_handle_stub()` case 5 (CLSID `0x0100101C`) passou a
     alocar objetos stub reais via `make_stub_interface()` em vez de escrever
     `NULL` nos out-params — a hipotese registrada na secao anterior deste
     documento sobre a causa da corrupcao de SP/VRAM. **Corrigido, mas ainda
     nao re-testado ponta a ponta contra a ROM real apos o fix** (ver
     checklist).
   - Family Pack: `glDrawArrays` (`GLFN_DrawArrays`) passou a chamar
     `draw_prim(mode, count, ...)` com os dados reais dos vertex arrays do
     jogo em vez de `draw_test_quad()` hardcoded.
   - Build: removido `Directory.Build.props` (toolset fixo causava falha de
     descoberta de SDK numa maquina); blocos `{}` adicionados em `case`
     labels para evitar redeclaracao de variavel.
4. **`7a55083` - Enable DrawArrays with live game rendering**: o jogo passou
   a renderizar via `GL_TRIANGLE_FAN` com texturas vinculadas de verdade
   (`transform_vertex() -> raster_triangle() -> pixels`). **Problemas
   conhecidos registrados no commit, ainda nao resolvidos**:
   - Vertex pointers as vezes chegam como `0x00000003` (endereco invalido) —
     precisa debugar a convencao de chamada nesse caso especifico.
   - Vertices computam mas as coordenadas de tela ficam fora da area visivel
     — sugere bug na transformacao (matriz/viewport), nao nos dados em si.
   - Frame time ~1.65s/frame no smoke test (180 frames em ~99s) — bem abaixo
     do alvo de 60 FPS; nao investigado ainda se e overhead do smoke test ou
     do rasterizador.

**Fixes desta sessao ainda sem confirmacao de teste real no RetroArch**: as
correcoes do Pac-Mania (case 5) e do `glDrawArrays` real foram compiladas e
validadas apenas por build bem-sucedido / smoke test parcial no momento em
que foram commitadas — o proximo passo pendente e testar as duas ROMs de
ponta a ponta no RetroArch com a DLL atual e atualizar esta secao com o
resultado real (rodou / travou / onde).

## Sessao 2026-07-11: Romset externalizado do repositorio

`1ffb04d` removeu `tests/roms/real/` (Pac-Mania, Double Dragon, Zeeboids,
Family Pack) e `tests/roms/make_test_rom.py` do repositorio Git — o pacote de
ROMs comerciais (~2.1 GB, 68 jogos) nao deveria estar versionado. As ROMs
agora vivem fora do repo, em
`C:\Users\Lucas\Downloads\zeebo-romset-and-devtools\Zeebo\Zeebo\Zeebo Game & App Compilation - OpenZeebo\`
(pacotes `.7z` por titulo, mais uma pasta `274804\` ja extraida com `mif/` e
`mod/` de varios jogos juntos, usada para varredura em lote). Ver
`tests/roms/README.md` para o guia atualizado e `docs/TESTING.md` para os
caminhos de teste corrigidos. **Consequencia**: qualquer trecho de doc/codigo
que ainda referencie `tests/roms/real/...` esta desatualizado — a fonte de
verdade agora e a pasta externa.

Nesta mesma sessao (ainda **nao commitado** no momento desta atualizacao de
documentacao):

- `analyze_clsids.ps1` (raiz do repo, nao rastreado): script exploratorio que
  varre a pasta `274804\mif` e `274804\mod` do romset externo e lista CLSIDs
  por jogo, priorizando os 10 primeiros titulos (Pac-Mania, Family Pack,
  Double Dragon, Zeeboids, Crash Bandicoot, Quake, Quake II, FIFA 09,
  Resident Evil 4, Super BurgerTime). Ferramenta de apoio para a estrategia
  de "trabalho paralelo em varios jogos", ainda nao integrada a nenhum
  pipeline de build/teste.
- `BLOCKERS_ANALYSIS.md` (raiz do repo, nao rastreado): levantamento tecnico
  dos bloqueadores atuais organizados por categoria (memoria/boot,
  renderizacao, input, CLSID por jogo), com checklist de teste por jogo e
  metricas-alvo por tier. Complementa este documento com uma visao mais
  tabular/checklist; nao substitui o diagnostico detalhado acima.
- `src/gpu/egl_gl.c`: estado GLES adicional aceito (ainda sem efeito visual
  na rasterizacao simples, exceto scissor): `GL_CULL_FACE`, `GL_DEPTH_TEST` e
  `GL_SCISSOR_TEST` agora sao rastreados via `glEnable`/`glDisable`; o
  scissor test **e** aplicado de verdade em `raster_triangle()` (recorta
  pixels fora do retangulo de `glScissor`). Alem disso, ~25 chamadas GLES que
  antes cariam no `default` generico agora tem `case` proprio e documentado
  como aceito-mas-sem-efeito (`glTexEnv*`, `glFog*`, `glLight*`,
  `glMaterial*`, `glCullFace`, `glDepthFunc/Mask/Rangex`, `glStencil*`,
  `glLineWidthx`, `glPointSizex`, `glPolygonOffsetx`, `glSampleCoveragex`,
  `glFrontFace`, `glShadeModel`, `glLogicOp`, `glHint`, `glPixelStorei`) —
  isso reduz ruido de log de "funcao GL desconhecida" sem mudar
  comportamento. `glReadPixels` ganhou implementacao real (le da VRAM,
  converte para `GL_RGBA/UNSIGNED_BYTE` ou RGB565 no buffer do guest).
- `src/loader/mod_loader.c`: `zmod_try_probe_packaged_assets()` agora tambem
  detecta (so log, sem parsing) um arquivo `.sig` companheiro do `.mod`
  quando presente — fato de compatibilidade documentado em
  `docs/THIRD_PARTY.md` (Infuse exige um `.sig` do mesmo nome ao lado do
  conteudo, embora nao valide o conteudo dele).

## Sessao 2026-07-11 (2): Loader varre o arquivo inteiro em busca de CLSID + escaneia .bar

`src/loader/mif_parser.c` (`scan_file_for_clsid()`): antes lia so os primeiros 8KB do arquivo
para procurar constantes de CLSID conhecidas; jogos com CLSID mais adiante no `.mif` (ou fora
dessa janela) nunca eram resolvidos por essa via, caindo no fallback de "nome de arquivo
conhecido" (so cobre os titulos ja catalogados manualmente) ou em CLSID desconhecido. Agora:

- Buffer aumentado de 8KB para 32KB e a leitura passa a **varrer o arquivo inteiro** em blocos
  (`while (fread(...) > 0)`), nao mais um unico chunk inicial.
- Alem do `.mif`, o loader agora tambem escaneia o `.bar` companheiro (quando presente) pela
  mesma tecnica, ja que alguns titulos podem ter o CLSID apenas nos recursos.
- Vazamento de file handle corrigido: o `fclose()` antigo rodava antes do loop de busca (arquivo
  ja fechado durante o scan em builds antigos que leem incrementalmente); agora fecha so ao sair
  do loop ou ao encontrar o CLSID.

Objetivo direto: aumentar a chance de resolver o CLSID de Zeeboids e de outros titulos alem do
Tier 1, sem exigir cadastro manual de nome de arquivo para cada um dos 68 jogos do romset.
**Ainda nao testado contra o romset completo** - proximo passo e rodar `analyze_clsids.ps1` (ou
o loader de verdade) contra os 68 titulos e registrar quantos CLSIDs novos aparecem.

## Sessao 2026-07-11 (3): Rodadas 2/3 multi-IA - os 4 jogos Tier 1 completam o boot

Duas rodadas de trabalho paralelo (3 IAs na rodada 2, prompt de 6 tarefas + sessao
unica na rodada 3) transformaram o estado do projeto. Relatorios detalhados:
`STATUS_A_EVENTO_DOUBLE_DRAGON.md`, `STATUS_B_RENDER_FAMILYPACK.md`,
`STATUS_C_BOOT_PACMANIA_ZEEBOIDS.md` (raiz do repo).

### Rodada 2 (diagnostico com evidencia)

1. **Event loop confirmado funcionando** (`9ea52b6`/`adeaa25`): `zboot_process_timers()`
   em `boot.c` + chamada em `retro_run()`. Timers do `IShell_SetTimer` disparam nos
   frames certos. De quebra, corrigiu uma suposicao errada do commit `72fbf38`:
   `PC=0xF0000A40` nao e API faltando - `ZTRAP_ID(0xF0000A40)=0x290=ZT_GUEST_RETURN`,
   o retorno normal de guest call.
2. **Pac-Mania: causa raiz real do descarrilamento** (`STATUS_C`): o fix antigo do
   case 5 nao era suficiente. Matematica de stack exata provou que `CreateInstance`
   era chamado num endereco sem prologo (meio de funcao) e o epilogo compartilhado
   estourava o SP em exatos 0x58 bytes para dentro da VRAM (`SP=0x30000048`,
   `0x30000048-0x58=0x2FFFFFF0` = SP inicial do cpu_reset).
3. **Zeeboids: CLSID confirmado byte a byte** (`6b0c89c`): `0x0108FF1A` no offset 8116
   do `.mif`. Com isso o jogo passou de "trava no AEEMod_Load" para 1M+ instrucoes -
   e a nova causa raiz foi isolada: o stub de scatter-load do armcc esperava a
   `Region$$Table` num layout que o loader nao reproduzia (r1=0 por design, CPU
   andava por RAM zerada ate 0x04000000).
4. **Family Pack: ponteiros de heap aceitos** em `decode_vertex_ptr()` (resgatado em
   `f4ba57d` depois de quase se perder por falta de isolamento de working tree -
   licao: rodadas multi-IA agora usam `git worktree`, nao so branches).

### Rodada 3 (implementacao dos fixes - branch `rodada3/sessao-unica`, merged em master)

1. **`e747bd8` - HLE do bootstrap PIC no loader** (o fix mais importante do projeto ate
   agora): o header `mod1` informa em `+0x18` o deslocamento do codigo real
   (tipicamente `0x200`); o loader agora copia code+data desse ponto direto para a
   VA 0, zera o BSS correto e dispensa o scatter-loader do armcc. **Resultado:
   Pac-Mania e Zeeboids passam por CreateInstance e EVT_APP_START sem descarrilar.**
2. **`65ae02e` - IDisplay real desenha**: o slot 5 do IDisplay e `DrawRect` (nao um
   criador de subobjetos, como a rodada 2 hipotetizou). Implementados DrawRect,
   Update, SetColor e recorte no layout BREW de 48 slots. **Double Dragon preenche o
   framebuffer de verdade** (preto -> branco por comando do jogo).
3. **`c8ae875` - Layout real do AEEMod**: `+4` e refcount, `pIShell` fica em `+8`
   (a correcao anterior escrevia por cima do refcount).
4. **`a2f6767` - CPU parada sem timers**: evita reexecutar o trap de retorno a cada
   frame. Zero "retornos em estado inesperado" na regressao Tier 1 - **mas ver
   regressao conhecida abaixo**.

### Estado verificado por smoke test (build do merge, 2026-07-11)

| Jogo | Boot | Depois do boot |
|---|---|---|
| Double Dragon | ate "rodando", sem descarrilar | timers ativos, `IDisplay_DrawRect` real pinta o framebuffer (fb `0xFF000000` -> `0xFFFFFFFF`) |
| Pac-Mania | ate "rodando", sem descarrilar | timers disparam callbacks continuamente (~70k instrucoes/60 frames); tela ainda preta |
| Zeeboids | ate "rodando", sem descarrilar | ~545k instrucoes no EVT_APP_START, depois idle (espera eventos) |
| Family Pack | ate "rodando", sem descarrilar | **REGRESSAO**: zero chamadas GL - ver abaixo |

### Regressao conhecida (bloqueio nº 1 para a proxima rodada)

O Family Pack agenda seu game loop via **signals** (`SignalCBFactory_CreateSignal`),
nao timers. Com a politica de `a2f6767` ("CPU fica parada quando nao ha timers"), a
CPU estaciona depois do boot e o loop de render GL que ja funcionou (glClear ->
glDrawArrays -> eglSwapBuffers, sessoes 2026-07-08) **parou de rodar** - confirmado
por smoke test: 0 ocorrencias de glClear/DrawArrays/eglSwapBuffers, instrucoes
congeladas em 729568 do frame 1 ao 121. Zeeboids esta no mesmo caso. O proximo passo
e dirigir signals (dispatch dos `ISignal` registrados, ou re-liberar a CPU quando ha
signals pendentes), sem desfazer o ganho de `a2f6767` para os jogos baseados em timer.

## Estatisticas
- Modulos implementados: ~45 arquivos .c/.h em `src/` (~15.500 linhas .c+.h; ~6.700 so em .c),
  distribuidos em `core/`, `cpu/`, `memory/`, `loader/`, `brew/`, `gpu/`, `audio/`, `input/`,
  `debug/` - crescimento significativo desde a contagem antiga de "27 arquivos / ~3500 linhas"
  (fase de esqueleto inicial); `src/gpu/egl_gl.c` sozinho hoje tem ~1400 linhas, o maior arquivo
  do projeto, refletindo o peso do HLE de EGL/GLES 1.x.
- Instrucoes ARM: cobertura completa do conjunto basico ARMv6 user-mode
- Performance: 60 MIPS no interpretador (Release x64)

## Atualizacao de documentacao
- [x] Status antigo de "skeleton" mantido apenas como historico
- [x] Base funcional atual descrita em README e docs principais
- [x] Licenca documentada como GPLv3
- [x] Fontes de referencia e possiveis codigos reutilizados registradas em documento proprio
- [x] 2026-07-11: docs sincronizados com o estado real pos-romset-externalizado
      (README/TESTING com caminhos `tests/roms/real/...` estavam quebrados
      apos `1ffb04d`; BLOCKERS_ANALYSIS.md e trabalho nao commitado de GL
      state/`.sig` incorporados a este documento)
