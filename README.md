# zeebo_libretro

**Core libretro/RetroArch para o Zeebo — interpretador ARM + HLE do sistema operacional BREW (AEE), escrito do zero em C.**

[![Licença](https://img.shields.io/badge/licença-GPL--3.0-blue)]()
[![Status](https://img.shields.io/badge/status-em%20desenvolvimento-orange)]()
[![Build](https://img.shields.io/badge/build%20validado-Windows%20%7C%20CMake%20%7C%20Ninja-lightgrey)]()

> Nenhum jogo é jogável ainda. Este README descreve honestamente até onde o projeto chega hoje —
> veja a seção [Status atual](#status-atual).

---

## Sumário

- [O que é isto?](#o-que-é-isto)
- [Status atual](#status-atual)
- [Arquitetura](#arquitetura)
- [Funcionalidades implementadas](#funcionalidades-implementadas)
- [Compilando](#compilando)
- [Como usar / Testando](#como-usar--testando)
- [Roadmap](#roadmap--próximos-passos)
- [Documentação](#documentação)
- [Licença e créditos](#licença-e-créditos)
- [Estrutura do repositório](#estrutura-do-repositório)

---

## O que é isto?

### O console Zeebo

O **Zeebo** foi um videogame lançado em 2009, principalmente no Brasil e no México, fruto de
uma parceria envolvendo a Qualcomm. Diferente de um console "normal", o Zeebo não rodava jogos
como binários nativos direto sobre o hardware: ele rodava sobre o **BREW** (Binary Runtime
Environment for Wireless, também chamado de **AEE** — Application Execution Environment), uma
plataforma da própria Qualcomm originalmente criada para celulares, no estilo do que o Java
ME/J2ME era para outros aparelhos da época. Os jogos do Zeebo são **applets BREW** compilados
para ARM, e não código que fala diretamente com registradores de hardware — eles chamam APIs de
sistema (`IShell`, `IDisplay`, `IFileMgr`, `ISound` etc.) do mesmo jeito que um app de celular
chamaria APIs do seu SO.

### O que este projeto faz

`zeebo_libretro` é um **core libretro** (a mesma tecnologia por trás dos cores de SNES, PS1 etc.
no RetroArch) que:

- **Interpreta CPU ARM/Thumb real** (não é um "emulador de BREW" abstrato — ele de fato executa
  as instruções ARM11/ARMv6 dos jogos, instrução por instrução);
- **Reimplementa em C o sistema operacional BREW/AEE via HLE** (High-Level Emulation) — ou seja,
  em vez de rodar a ROM original do BREW da Qualcomm (que não está disponível/não é viável usar),
  o core fornece sua própria implementação das centenas de funções de sistema que os jogos
  chamam, do mesmo jeito que um emulador de N64 ou PS1 faz HLE da BIOS, só que aqui a camada
  inteira de sistema operacional é HLE'd, não só o boot;
- Consegue **carregar ROMs comerciais reais** (arquivos `.mod`/`.mif` extraídos de cartuchos/dumps
  do Zeebo) e executá-las de verdade até certo ponto do processo de inicialização do applet BREW.

Este é um projeto solo, hobby, em desenvolvimento ativo e inacabado — não é (ainda) um "baixe e
jogue".

---

## Status atual

**Marco de 2026-07-11 (rodadas 2/3 de desenvolvimento): os 4 jogos do Tier 1 agora
completam o boot inteiro** — `AEEMod_Load → IModule_CreateInstance → EVT_APP_START →
"rodando"` — **sem nenhum descarrilamento de CPU**, confirmado por smoke test contra as
ROMs reais. Nenhum jogo produz frames de *gameplay* ainda (o que aparece na tela vai de
tela preta a preenchimento branco real via `IDisplay_DrawRect`), mas os dois piores
bloqueios históricos do projeto — o estouro de stack do Pac-Mania e o scatter-load do
Zeeboids — foram resolvidos de verdade, com causa raiz comprovada por desmontagem.

### O que já funciona de ponta a ponta

- Carrega arquivos `.mod`/`.mif` comerciais reais do Zeebo (não apenas ROMs sintéticas de teste).
- Executa código ARM/Thumb real dos jogos através do interpretador de CPU.
- **Os 4 jogos prioritários completam a máquina de estados real de boot do BREW** sem
  descarrilar: `AEEMod_Load → IModule_CreateInstance → EVT_APP_START → rodando`.
- O loader agora faz **HLE do bootstrap PIC/scatter-load do armcc** (`mod_loader.c`): módulos
  `mod1` com o código real deslocado no arquivo (`+0x200`) são mapeados direto na VA 0 com BSS
  correto, pulando o stub de relocação que derrubava Pac-Mania e Zeeboids.
- Motor de eventos real: `IShell_SetTimer` + `zboot_process_timers()` em `retro_run()` —
  timers do jogo expiram e disparam callbacks reais no guest (confirmado no Pac-Mania e
  Double Dragon).
- **Double Dragon pinta a tela de verdade** via `IDisplay_DrawRect` real (layout BREW de 48
  slots) — o framebuffer sai de preto para branco por comando do próprio jogo.
- O rasterizador GLES 1.x mínimo (`egl_gl.c`) já desenhou um quadrilátero com gradiente via
  `glDrawArrays` real em sessões anteriores (Family Pack).

### Resultado real por ROM comercial (smoke test de 2026-07-11, pós-rodada 3)

| ROM | CLSID | Progresso atual |
|---|---|---|
| **Double Dragon** | `0x0102F789` | Boot completo até "rodando". Timers ativos, e o jogo **desenha de verdade**: `IDisplay_DrawRect` real preenche o framebuffer (preto → branco confirmado no smoke test). O mais próximo de mostrar imagem reconhecível. |
| **Pac-Mania** | `0x01087B72` | Boot completo até "rodando" **sem descarrilar** — o estouro de stack (SP→VRAM) foi resolvido pelo HLE do bootstrap PIC no loader. Timers disparam callbacks continuamente (~70k instruções/60 frames). Tela ainda preta: falta o caminho de desenho que o jogo usa. |
| **Zeebo Family Pack** | `0x010903C6` | Boot completo até "rodando". **Regressão conhecida**: o jogo agenda trabalho via *signals* (`SignalCBFactory`), não timers — e a política atual de "CPU parada sem timers" (`a2f6767`) deixa a CPU estacionada após o boot, então o loop de render GL que existia antes **parou de rodar**. Reativar esse caminho é o bloqueio nº 1 atual. |
| **Zeeboids** | `0x0108FF1A` | Boot completo até "rodando" (antes travava dentro do próprio `AEEMod_Load`). CLSID confirmado byte a byte no `.mif`. Executa ~545k instruções no `EVT_APP_START` e fica idle — mesmo caso do Family Pack (espera eventos que ainda não entregamos). |

### Por que nada é "jogável" ainda

- **Signals não disparam**: Family Pack e Zeeboids agendam seu game loop via
  `ISignal`/`SignalCBFactory`, e o core ainda só dirige *timers* — a CPU fica parada
  esperando um evento que nunca chega. É o próximo alvo prioritário.
- **Caminhos de desenho incompletos**: Double Dragon já usa `IDisplay_DrawRect` real, mas
  texto/bitmap/blit do IDisplay e a cobertura GLES (blend, depth, texturas completas)
  seguem mínimos — então "desenhar" hoje significa retângulos e clear, não cenas.
- Não há save states, nem exposição de memória para RetroAchievements/cheats — ambos
  explicitamente adiados ("fase 7"/futuro) no código atual.

Ou seja: o core **não trava o RetroArch e não finge que funciona** — quando algo dá errado, a
CPU emulada para de forma limpa e loga o motivo (`CPU descarrilou: fetch em 0x...`), em vez de
continuar silenciosamente ou derrubar o processo. Isso é uma escolha de design, não um acidente.

---

## Arquitetura

### Por que HLE, e não "rodar a ROM real"

Um emulador de console clássico (NES, PS1, N64...) normalmente tenta rodar a BIOS/firmware
original do hardware, ou reimplementá-la via HLE só para o boot — o jogo em si roda como código
nativo direto contra a "máquina" emulada. O Zeebo é diferente na raiz: **o próprio jogo é um
applet de um sistema operacional de terceiros (BREW/AEE)**, não código bare-metal. Não existe
"pular a BIOS e cair no jogo": o jogo *é* uma sequência de chamadas de API do BREW do início ao
fim (criar o applet, tratar eventos, desenhar via `IDisplay`, tocar som via `ISound`, ler
arquivos via `IFileMgr`, etc.).

Como a ROM do BREW real da Qualcomm não está disponível para uso legal neste projeto, a única
opção viável é **HLE puro da camada inteira de sistema operacional**: o core interpreta o ARM
real do jogo, mas toda vez que o jogo chama uma API do BREW, quem responde é a reimplementação em
C deste projeto — não o BREW original. Essa foi uma decisão de arquitetura deliberada desde o
início (documentada em `docs/PLANNING_ARCHIVE.md`), não um atalho de última hora, e é o mesmo
caminho adotado por outras referências de emulação do Zeebo (Infuse, Zeemu).

### CPU (`src/cpu/`)

Interpretador puro (sem dynarec/JIT) de ARM11/ARMv6, **modo usuário apenas** — faz sentido porque
a camada de sistema (que normalmente rodaria em modo privilegiado) é HLE'd, não interpretada.
Cobre o conjunto de instruções ARM básico completo (processamento de dados com todos os 16
opcodes, família de multiplicação, todas as formas de load/store incluindo half-word/signed/LDRD,
STRD, LDM/STM com todos os modos de endereçamento, branches com interworking ARM↔Thumb) e todos
os 19 formatos clássicos do Thumb-1 mais extensões comuns do ARMv6 (`SXTB/SXTH/UXTB/UXTH`,
`REV/REV16/REVSH`). Barrel shifter único e compartilhado (`decode.c`) e flags NZCV calculadas via
aritmética de 64 bits para carry/overflow corretos (`flags.c`).

Quando o PC do guest sai de qualquer região executável válida (RAM, heap ou stack), a CPU **para
de forma limpa** em vez de continuar ou travar o processo — loga o erro e despeja um ring buffer
com os últimos 64 PCs executados (`src/debug/trace.c`) para facilitar o diagnóstico.

### Memória (`src/memory/`)

Mapa de memória fixo e simples:

| Região | Base | Tamanho |
|---|---|---|
| RAM (código + dados) | `0x00000000` | 64 MB |
| Heap | `0x10000000` | 32 MB |
| Stack | `0x2FC00000` | 4 MB |
| VRAM / framebuffer | `0x30000000` | 2 MB |
| Trap HLE (endereços mágicos) | `0xF0000000`–`0xF0001000` | 1024 "endereços" de API |

Política de acesso: **nunca trava** — leitura fora dos limites loga e retorna 0, escrita fora dos
limites é descartada silenciosamente (com log limitado para não poluir a saída). Um alocador de
heap por blocos com first-fit e coalescing (`heap.c`) serve `MALLOC`/`FREE`/`REALLOC` de dentro
da própria memória emulada.

### Loader (`src/loader/`)

Responsável por transformar um arquivo `.mod`/`.mif` em memória emulada pronta para execução e
por resolver qual CLSID de applet o jogo expõe (`mod_loader.c`, `mif_parser.c`). A resolução de
CLSID tenta, em ordem: (1) ler o `.mif` companheiro (metadados legíveis), (2) escanear
constantes conhecidas dentro do próprio `.mif` **e**, quando presente, do `.bar` companheiro
(varredura de arquivo inteiro, não só os primeiros bytes), (3) inferir pela convenção de path
`mod/<id>/<jogo>.mod` do dump. Também detecta (sem validar conteúdo) um `.sig` companheiro, fato
de compatibilidade observado no Infuse — ver `docs/THIRD_PARTY.md`. Desde 2026-07-11 o loader
também faz **HLE do bootstrap PIC do armcc**: módulos `mod1` com código real deslocado no
arquivo (`+0x18` do header, tipicamente `0x200`) são copiados direto para a VA 0 com BSS
correto, dispensando o scatter-loader que descarrilava Pac-Mania/Zeeboids.
`analyze_clsids.ps1` na raiz varre o romset externo em busca de CLSIDs de outros títulos.

### HLE do BREW/AEE (`src/brew/`)

O subsistema mais importante do projeto. Cada "chamada de API" do jogo é, na prática, um branch
para um endereço mágico na janela `0xF0000000`–`0xF0001000`; quando o loop de fetch da CPU vê o PC
cair ali, ele desvia para o dispatcher de traps (`trap_dispatch`, `src/brew/brew.c`) em vez de
tentar decodificar uma instrução. O handler lê argumentos de `R0`–`R3` (e da stack, para
argumentos extras), executa o comportamento emulado e devolve o controle ao guest via `BX LR`.

Duas superfícies de API coexistem:

1. **Tabela de funções auxiliares `AEEHelperFuncs`** (`src/brew/helpers.c`) — 117 slots nomeados
   batendo com o layout real do BREW (memmove, sprintf, malloc/free/realloc, GetRand,
   GetTimeMS...), dos quais **44 têm lógica real implementada**; o resto cai num handler padrão
   que loga um aviso (limitado) e devolve 0.
2. **`IShell` real** (`src/brew/boot.c`) — vtable de 128 slots batendo com a ordem real de
   métodos do `AEEShell.h`, dos quais **~21 têm comportamento real** (CreateInstance,
   GetDeviceInfo, SetTimer/CancelTimer com um array de 16 timers de verdade, etc.); os demais
   devolvem `EFAILED` de forma controlada. Um mecanismo genérico de "objeto COM stub"
   (`make_stub_interface()`) satisfaz `CreateInstance`/`QueryInterface` para cerca de 40 class
   IDs conhecidos do BREW (display, filemgr, codecs de mídia, HID, EGL/GL, etc.) — a maioria são
   placeholders inertes, mas alguns têm lógica real por cima (HID ligado ao RetroPad de verdade,
   `IDisplay_GetDeviceBitmap` apontando para a VRAM real).

A máquina de estados de boot do applet (`boot.c`) segue a sequência real do BREW:
`AEEMod_Load → IModule_CreateInstance → EVT_APP_START → rodando`, incluindo contornos para
peculiaridades observadas em ROMs reais (correção de ponteiros de vtable inválidos, duplicação de
argumentos entre registradores para tolerar stubs de bootstrap ROPI diferentes).

### GPU e áudio (`src/gpu/`, `src/audio/`)

Framebuffer fixo de 640×480 em XRGB8888, apoiado diretamente na VRAM emulada, com preenchimento,
retângulos, linhas (Bresenham) e blit com chave de transparência para o caminho `IDisplay` 2D
clássico. Além disso, `src/gpu/egl_gl.c` (hoje o maior arquivo do projeto, ~1400 linhas) HLE'a
`AEECLSID_EGL`/`AEECLSID_GL` — a convenção de chamada "sem `this`" do wrapper GL da Qualcomm foi
decodificada (ver `docs/PROGRESS.md`), e um rasterizador de software mínimo (`raster_triangle()`)
já desenha `GL_TRIANGLE_FAN` texturizado de verdade a partir de `glDrawArrays`, com matrizes
fixed-point, scissor test aplicado e `glReadPixels` real. Boa parte do resto do estado GLES 1.x
(`glFog*`, `glLight*`, `glMaterial*`, depth/stencil...) é aceita mas ainda não afeta a
rasterização. Mixer de áudio de 16 vozes, 44.1kHz estéreo, com resampling de ponto fixo a partir
de PCM de 8 ou 16 bits — tudo acionado pelas mesmas traps HLE de `ISound`/`IDisplay`/`IBitmap`.

---

## Funcionalidades implementadas

- **CPU**: interpretador ARM11/ARMv6 modo usuário + Thumb-1 completo, sem JIT/dynarec.
- **Memória**: mapa fixo RAM 64MB / heap 32MB / stack 4MB / VRAM 2MB, acesso "nunca trava",
  alocador de heap com merge de blocos livres.
- **Loader**: `.mod` (variante com header `BREW` e variante binário ARM cru) e resolução de CLSID
  do applet via MIF real, nome de arquivo conhecido ou escaneamento de constantes no binário.
  `.mif`: extração heurística de nome (parsing estrutural completo ainda pendente). `.bar`:
  apenas detecção/existência (parsing ainda pendente). `zip`: aceito como conteúdo no frontend
  libretro (extração delegada ao frontend), com fallback de load por `path` no core.
  Para dumps no formato `mod/<id>/<jogo>.mod`, a base de assets passa a usar a raiz do pacote,
  facilitando acesso a `mif/`, `music/`, `images/` etc.
- **BREW HLE**: 117 slots de `AEEHelperFuncs` (44 implementados) + 128 slots de `IShell` real
  (~21 implementados) + stubs COM genéricos para ~40 class IDs conhecidos.
- **GPU**: framebuffer 640×480 XRGB8888 (fill/rect/linha/blit com transparência) **+** HLE de
  `EGL`/`GLES 1.x` com rasterizador de software mínimo: `glDrawArrays` (`GL_TRIANGLE_FAN`)
  desenha triângulos texturizados de verdade, scissor test funcional, `glReadPixels` real; estado
  de fog/light/material/depth/stencil aceito mas ainda sem efeito na rasterização.
- **Áudio**: mixer 44.1kHz estéreo, 16 vozes, PCM U8/S16 com resampling — sem MIDI/MP3/ADPCM.
- **Input**: RetroPad mapeado para um bitmask de botões Zeebo, entregue ao guest via o mecanismo
  de sinal de evento HID do BREW.
- **Debug**: log nivelado com prefixo `[Zeebo]`, ring buffer de trace de PC, classificador
  grosseiro de instrução (não é um disassembler completo — isso ainda é TODO).
- **Sem** save states, sem exposição de memória para cheats/RetroAchievements (ambos adiados de
  propósito).

---

## Compilando

### Windows (Visual Studio 2022 ou CMake/Ninja) — caminho validado

Abra `zeebo_libretro.sln` no Visual Studio 2022 (workload "Desenvolvimento para desktop com
C++"), selecione a configuração `Release`/`x64` e compile (`Ctrl+Shift+B`), ou via linha de
comando (Developer PowerShell):

```powershell
msbuild zeebo_libretro.sln /p:Configuration=Release /p:Platform=x64
```

Ou pelo caminho multiplataforma novo com `CMake`:

```powershell
cmake --preset desktop-release
cmake --build --preset desktop-release
```

Resultado via CMake no Windows: `build/desktop-release/zeebo_libretro.dll`.

> O repositório chegou a incluir um `Directory.Build.props` fixando versões de SDK/toolset para
> contornar um problema de descoberta automática de SDK numa máquina específica de
> desenvolvimento — esse arquivo foi **removido** (`aa3dfe2`, 2026-07-10) porque o
> auto-discovery do toolset funciona normalmente numa instalação padrão do Visual Studio 2022. Se
> o build falhar por erro de toolset/SDK não encontrado numa máquina diferente, passe os valores
> explicitamente, por exemplo:
> ```powershell
> msbuild zeebo_libretro.sln /p:Configuration=Release /p:Platform=x64 `
>     /p:VCToolsInstallDir="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\" `
>     /p:VCToolsVersion=14.44.35207
> ```
> Um atalho de build rápido também está disponível em `quick_build.ps1` na raiz do repo.

### Linux/macOS (Makefile ou CMake)

O `Makefile` da raiz foi atualizado para refletir o conjunto atual de fontes do núcleo completo.
Em desktop, tanto `make` quanto `CMake` passam a ser caminhos viáveis de build.

Exemplo com `make`:

```bash
make
```

Exemplo com `CMake`:

```bash
cmake --preset desktop-release
cmake --build --preset desktop-release
```

No Linux o resultado esperado é `zeebo_libretro.so`; no macOS, `zeebo_libretro.dylib`.

### Android / Web / toolchains cruzados

O repositório agora inclui `CMakePresets.json` com presets preparados para `Android`, `UWP` e
`Emscripten/Web`. Isso não substitui o SDK/toolchain real de cada alvo, mas reduz o retrabalho
quando houver ambiente disponível.

Guia resumido:

- `cmake --preset android-arm64`
- `cmake --preset android-armv7`
- `cmake --preset android-x86_64`
- `cmake --preset emscripten`
- `cmake --preset uwp`

Detalhes e limitações reais estão em `docs/PORTABILITY.md`.

### Dependências externas

Nenhuma, além de um compilador C e do toolchain do alvo. O core é C autocontido — nem `zlib`,
`libpng`, SDL ou similares são usados dentro de `src/`.

---

## Como usar / Testando

### Via RetroArch

1. Copie a DLL compilada para a pasta de cores do RetroArch, por exemplo:
   ```powershell
   Copy-Item -Path "x64\Release\zeebo_libretro.dll" -Destination "C:\RetroArch\cores\" -Force
   ```
   (ajuste o destino para a instalação real, ex. `E:\SteamLibrary\steamapps\common\RetroArch\cores\`)
2. Abra o RetroArch → **Load Core** → procure por "Zeebo" (deve aparecer com uma string de
   versão, ex. `0.2-cpu`).
3. **Load Content** → selecione um arquivo `.mod` ou `.mif` (extensões aceitas hoje:
   `valid_extensions = "mod|mif|zip"`). As 4 ROMs do Tier 1 estão em `tests/roms/`
   (`real_pacmania_game.mod`, `real_family_pack_game.mod`, `real_ddragon_game.mod`,
   `real_zeeboids_game.mod`); o romset completo (68 jogos, ~2.1 GB) vive fora do repo em
   `C:\Users\Lucas\Downloads\zeebo-romset-and-devtools\` (ver `docs/TESTING.md`).
4. Resultado esperado depende do jogo — veja a tabela em [Status atual](#status-atual). Em
   nenhum caso o RetroArch em si deve travar: se o boot emperrar, é a CPU emulada que para
   sozinha e loga `CPU descarrilou`.

### Via smoke test (mais rápido — sem abrir a UI do RetroArch)

`tests/libretro_smoke.c` é um host libretro mínimo que carrega o core dinamicamente e roda 180
frames, sem depender da interface do RetroArch — o método preferido para iteração rápida durante o
desenvolvimento. No Windows, o binário gerado é `libretro_smoke.exe`; em plataformas POSIX ele usa
`dlopen`.

Via CMake:

```powershell
cmake --preset desktop-smoke
cmake --build --preset desktop-smoke
```

Execução (ROM apontando para a pasta externa do romset — ver `docs/TESTING.md`):

```powershell
build\desktop-smoke\libretro_smoke.exe build\desktop-smoke\zeebo_libretro.dll `
    "C:\Users\Lucas\Downloads\zeebo-romset-and-devtools\Zeebo\Zeebo\Zeebo Game & App Compilation - OpenZeebo\274804\mod\276212\pacmania.mod"
```

Troque o ID (`276212`) por `277229` (Family Pack), `274754` (Double Dragon) ou `279382`
(Zeeboids) para testar outros títulos prioritários. A saída (stderr) mostra linhas
`[INFO]/[ERROR] [Zeebo] ...` de progresso do boot, linhas
periódicas `[VIDEO] 640x480 pitch=... first=0x########` confirmando que `retro_run` está
produzindo frames, e, em caso de falha, uma linha `CPU descarrilou: fetch em 0x... (LR=...
SP=...)` com um dump de trace de 64 PCs. Detalhes completos, incluindo como ler um crash, em
`docs/TESTING.md`.

---

## Roadmap / Próximos passos

Da seção "Próximos Passos" de `docs/PROGRESS.md`:

- [ ] **Dirigir signals** (`ISignal`/`SignalCBFactory`): Family Pack e Zeeboids agendam o
      game loop por signals, não timers — hoje a CPU fica estacionada depois do boot e o
      loop de render do Family Pack (que já funcionou) parou de rodar. Bloqueio nº 1.
- [ ] Expandir o IDisplay real além do `DrawRect` (DrawText, BitBlt, bitmaps) — Double
      Dragon já desenha retângulos reais; o próximo nível é imagem reconhecível.
- [ ] Re-validar a transformação/viewport do rasterizador GLES quando o loop GL do Family
      Pack voltar a rodar (fix de ponteiros de vértice já resgatado em `f4ba57d`).
- [ ] Rodar `analyze_clsids.ps1`/smoke test contra o romset completo (68 jogos) e medir
      quantos títulos resolvem CLSID e completam o boot com o loader atual (o HLE do
      bootstrap PIC deve destravar vários módulos `mod1` além do Tier 1).
- [ ] Parsing estrutural completo de MIF e BAR (hoje: extração heurística de nome e apenas
      detecção de existência, respectivamente).
- [ ] Popular `tests/test_cpu.c` e `tests/test_memory.c` (hoje são arquivos vazios/placeholder —
      não existe suíte de testes automatizados de CPU/memória ainda).
- [ ] Save states (serialização de CPU + memória).
- [ ] Áudio completo: ADPCM/MP3/MIDI (hoje só PCM cru via mixer).
- [ ] Validar os presets e toolchains cruzados em hardware/frontend real (`Linux`, `macOS`,
      `Android`, `UWP`, `Web`, e, quando possível, ports de console/homebrew).
---

## Documentação

| Arquivo | Conteúdo |
|---|---|
| [`docs/PROGRESS.md`](docs/PROGRESS.md) | Timeline de desenvolvimento, estado módulo a módulo, diagnóstico detalhado do bug atual do Pac-Mania e checklist de próximos passos. A fonte mais atual sobre "onde o projeto está". |
| [`docs/TESTING.md`](docs/TESTING.md) | Guia de teste: smoke test, teste manual no RetroArch, tabela de estado esperado por ROM, e como ler um crash de "CPU descarrilou". |
| [`docs/THIRD_PARTY.md`](docs/THIRD_PARTY.md) | Licenças e decisões de reuso de material de terceiros — o rastreador canônico para qualquer coisa referenciada de outro projeto. |
| [`docs/PLANNING_ARCHIVE.md`](docs/PLANNING_ARCHIVE.md) | Arquivo do raciocínio de design pré-código (por que HLE, por que libretro como camada fina, glossário técnico) — não repete o que já está em PROGRESS.md. |
| [`docs/PORTABILITY.md`](docs/PORTABILITY.md) | Guia de portabilidade: presets de CMake, famílias de plataforma, o que já está preparado no build e o que ainda depende de SDK/hardware/toolchain real. |
| [`BLOCKERS_ANALYSIS.md`](BLOCKERS_ANALYSIS.md) | Levantamento tabular dos bloqueadores atuais por categoria (memória/boot, renderização, input, CLSID por jogo), checklist de teste por jogo e métricas-alvo por tier — visão complementar mais rápida de consultar que a timeline de `docs/PROGRESS.md`. |

---

## Licença e créditos

O projeto é distribuído sob **GPL-3.0** (ver [`LICENSE`](LICENSE)). A migração de MIT para
GPL-3.0 foi feita deliberadamente para permitir adaptar/aprender de outros projetos de emulação de
Zeebo licenciados sob GPL — prática padrão entre cores libretro.

Este projeto foi construído com engenharia reversa própria mais o estudo de outros projetos como
**referência de comportamento e de formato de arquivo** — o código do BREW/AEE HLE, do
interpretador de CPU e do loader deste repositório foi **reimplementado de forma independente em
C**, não copiado de nenhum dos projetos abaixo. O rastreamento completo, por asset, está em
`docs/THIRD_PARTY.md`; resumo:

- **[Zeemu](https://github.com/mrpostiga/zeemu)** (GPL-3.0) — a referência primária de
  comportamento para `IShell`, `AEEApplet`, `IHID`, `ISignal`, `IFileMgr` e o comportamento de
  device-bitmap do `IDisplay`. Mantido apenas como cópia local de referência, não commitada.
- **Infuse** (licença própria, não-comercial, sem redistribuição) — usado só como referência de
  fatos de compatibilidade documentados publicamente (layout de conteúdo, uso de `.sig`, layout de
  input). Sua licença não é compatível com cópia de código para este projeto GPL-3.0, então nada
  foi copiado; o pacote local foi removido do repositório depois de extraídos os fatos de
  compatibilidade, e nunca foi publicado no remoto público.
- **[GGZ BREW Tools](reference/ggzbrewtools-main/)** (estilo zlib, permissiva) — documenta o
  formato de container GGZ (tabela big-endian + membros GZIP concatenados) usado por alguns jogos
  BREW/Zeebo. Compatível para reuso mantendo aviso de licença/copyright.
- **OpenZeebo** e **zeebo_doom** — mantidos como referências locais (gitignored, não commitadas)
  de engenharia reversa de hardware real e de outro app BREW homebrew rodando no console real,
  respectivamente; não estão atualmente conectados ao core.

---

## Estrutura do repositório

```
zeebo_libretro/
├── src/                    # Código-fonte do core (compilado hoje via .vcxproj), ~15.5k linhas
│   ├── core/                → integração libretro (retro_*), loop principal
│   ├── cpu/                 → interpretador ARM/Thumb
│   ├── memory/               → mapa de memória, heap
│   ├── loader/                → parsing de MOD/MIF/BAR, resolução de CLSID do applet
│   ├── brew/                 → HLE do BREW/AEE (IShell, helpers, boot lifecycle)
│   ├── gpu/                  → framebuffer 2D + HLE de EGL/GLES 1.x (egl_gl.c, o maior arquivo)
│   ├── audio/                 → mixer PCM
│   ├── input/                 → RetroPad → bitmask Zeebo
│   └── debug/                  → log, trace de PC, classificador de instrução
├── tests/
│   ├── libretro_smoke.c     # host libretro mínimo p/ teste rápido sem RetroArch
│   ├── test_cpu.c / test_memory.c  # placeholders vazios (ainda sem suíte automatizada)
│   └── roms/
│       └── README.md          # aponta para o romset externo (ROMs comerciais não ficam no repo)
├── docs/                    # PROGRESS.md, TESTING.md, THIRD_PARTY.md, PLANNING_ARCHIVE.md
├── BLOCKERS_ANALYSIS.md     # levantamento tabular de bloqueadores + checklist por jogo
├── analyze_clsids.ps1       # script exploratório: varre o romset externo listando CLSIDs por jogo
├── quick_build.ps1          # atalho de build rápido (Release x64)
├── reference/               # cópias locais de projetos de referência (majoritariamente gitignored)
├── zeebo_libretro/          # arquivos de projeto do Visual Studio (.vcxproj)
├── zeebo_libretro.sln       # solução Visual Studio 2022
├── Makefile                 # build Linux/macOS (hoje desatualizado, ver seção Compilando)
└── CMakeLists.txt           # placeholder, sem alvos ainda
