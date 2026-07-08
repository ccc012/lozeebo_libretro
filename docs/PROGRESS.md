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
- [x] `retro_load_game` com fallback por `path` (quando o frontend nao entrega `info->data`) e extensao `zip` habilitada no `valid_extensions`; extração permanece no frontend
- [x] Base de assets ajustada para dumps no layout `mod/<id>/<jogo>.mod` (raiz do pacote passa a ser usada para `IFileMgr`)

**Pac-Mania**: passa por `AEEMod_Load`, resolve o CLSID `0x01087B72` e entra em `IModule_CreateInstance`. A classe BREW `0x0100101C` e criada como stub, com os dois resultados do metodo 5 inicializados. Ficava presa em loop infinito dentro do `CreateInstance` (ver diagnostico e correcao na secao "Bloqueio atual" abaixo).

**Zeebo Family Pack**: CLSID `0x010903C6` extraido do MIF real. O applet passa por `AEEMod_Load`, `IModule_CreateInstance`, inicializacao de display, arquivos, som e joystick:
- `AEEHelper_GetAppInstance` implementado com base no comportamento do Zeemu.
- `SignalCBFactory_CreateSignal` cria e devolve objetos validos nos dois parametros de saida (padrao de referencia para stubs de classe, ver bug do Pac-Mania abaixo).
- `IHID_GetConnectedDevices`, `GetDeviceInfo` e `CreateDevice` expoem um joystick Zeebo valido.
- `IFileMgr` real conectado ao boot; o jogo ja tenta abrir `udata\highscore.dat`.
- `IDisplay_GetDeviceBitmap` devolve um bitmap RGB565 de 640x480 associado a VRAM.
- `CreateInstance` termina com applet valido; `EVT_APP_START=0` e tratado e o estado chega a "rodando" sem descarrilar.
- Bloqueio: o Family Pack cria `AEECLSID_EGL` e `AEECLSID_GL`, ainda stubs - o applet roda sem produzir frames. Proxima etapa: portar a pilha EGL/OpenGL ES GPLv3 do Zeemu e conecta-la ao framebuffer libretro.

## Proximos Passos (Fase 2.2/5: jogos reais)

### Bloqueio atual - Pac-Mania: SP sai da stack real e entra na VRAM durante o CreateInstance
- **Sintoma original**: durante `IModule_CreateInstance` do Pac-Mania (CLSID `0x0100101C`), o SP do guest crescia 0x58 bytes a cada iteracao ate a CPU terminar em `0xEA00000C`, sem qualquer sinal diagnosticavel do que estava acontecendo.
- **Causa raiz encontrada**: a guarda de trap HLE em `src/cpu/cpu.c` (`if (pc >= ZMEM_HLE_BASE)`) nao tinha limite superior. Qualquer PC corrompido/descarrilado `>= 0xF0000000` era silenciosamente absorvido como uma falsa "chamada de API nao implementada" (logada como aviso inofensivo, retornando `EFAILED` e retomando o guest via `BX LR`) em vez de ser sinalizado como crash. Isso deixava o codigo do Pac-Mania preso num loop infinito dentro do `CreateInstance`, crescendo o SP em 0x58 bytes por iteracao, de forma invisivel.
- **Correcao aplicada e verificada**: adicionado `ZMEM_HLE_END` (`0xF0001000u`) em `src/memory/memory.h`; a guarda do `cpu.c` passou a ser `if (pc >= ZMEM_HLE_BASE && pc < ZMEM_HLE_END)`. Qualquer coisa fora da janela real de trap agora cai na checagem ja existente de `pc_fetchable()`/"CPU descarrilou". Confirmado via rebuild + execucao contra a ROM real do Pac-Mania: o emulador agora para de forma limpa com um trace completo de PC em vez de entrar em loop silencioso para sempre.
- **Nova evidencia de diagnostico (pos-fix)**: com a guarda corrigida, a CPU descarrila de forma limpa em `PC=0xFF000000`, com `LR=0x0000115C` e, principalmente, `SP=0x30000048` no momento do descarrilamento - dentro da faixa de endereco da VRAM (`0x30000000-0x301FFFFF`), e nao da regiao real de stack (`0x2FC00000-0x2FFFFFFF`). A VRAM e pre-preenchida com a palavra literal de 32 bits `0xFF000000` na inicializacao (`framebuffer.c`, `zfb_clear(0xFF000000u)`, cor de limpeza), o que explica por que qualquer leitura de uma "stack pointer" residente em VRAM retorna exatamente `0xFF000000` e acaba sendo usada como destino de branch. O trace mostra o guest saltando `0xF0000C04 -> 0x115C -> 0x1160 -> 0x1164 -> 0xEB4 -> 0xEB8 -> 0xFF000000` antes de descarrilar.
- **Proximo passo (ainda nao feito)**: descobrir por que o SP sai da regiao real de stack (`0x2FC00000-0x2FFFFFFF`) e entra na VRAM (`0x30000000+`) durante o tratamento do `CreateInstance` do Pac-Mania. Hipotese principal (nao confirmada): `zbrew_handle_stub()` em `src/brew/boot.c`, no case 5 (CLSID `0x0100101Cu`), hoje escreve `NULL` nos dois out-params de stack do caller em vez de construir sub-objetos de stub reais - diferente do padrao que ja funciona no case 3 (`SignalCBFactory`, no mesmo arquivo), que aloca objetos reais via `make_stub_interface()`. Isso pode estar empurrando o codigo do guest para um caminho de fallback nunca testado que calcula mal seu proprio frame de stack. Tambem foi corrigido, nesse mesmo bloco (case 5), um comentario obsoleto que afirmava incorretamente que os out-params ficavam "no default 0xFF da stack" - isso nao procede: `memory.c` zera tudo na inicializacao, nao ha veneno de stack 0xFF neste modelo do emulador.

### Checklist
- [ ] Reverse engineering do formato MOD real (Ghidra) - entry point, relocacao completa
- [ ] Vtables/class IDs reais do BREW (RE do Infuse/documentacao arquivada) - CLSIDs do stub `0x0100101C`, Pac-Mania (`0x01087B72`) e Zeebo Family Pack (`0x010903C6`) ja resolvidos via MIF real; demais titulos ainda pendentes
- [x] IShell_SetTimer + inicio do event loop de applet: `EVT_APP_START=0` tratado, Zeebo Family Pack chega ao estado "rodando"
- [x] Testar com ROM real do Zeebo Family Pack: boot completo ate "rodando" (ver secao de validacao acima)
- [ ] Resolver o desvio do SP para a VRAM durante o `CreateInstance` do Pac-Mania (bloqueio atual, ver diagnostico acima)
- [ ] Portar a pilha EGL/OpenGL ES GPLv3 do Zeemu e conecta-la ao framebuffer libretro (bloqueio atual do Zeebo Family Pack)
- [ ] Popular `tests/test_cpu.c` e `tests/test_memory.c` (ainda vazios)
- [ ] Save states (serialize da CPU+memoria)
- [ ] ADPCM/MP3/MIDI (audio completo)

## Estatisticas
- Modulos implementados: 27 arquivos .c/.h (~3500 linhas)
- Instrucoes ARM: cobertura completa do conjunto basico ARMv6 user-mode
- Performance: 60 MIPS no interpretador (Release x64)

## Atualizacao de documentacao
- [x] Status antigo de "skeleton" mantido apenas como historico
- [x] Base funcional atual descrita em README e docs principais
- [x] Licenca documentada como GPLv3
- [x] Fontes de referencia e possiveis codigos reutilizados registradas em documento proprio
