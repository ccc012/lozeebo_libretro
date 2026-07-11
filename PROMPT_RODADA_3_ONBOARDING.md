# 🎯 PROMPT — Rodada 3: 6 IAs em Paralelo (zeebo_libretro)

> Cole este arquivo inteiro na conversa de cada uma das 6 IAs. A atribuição de tarefa é
> feita na hora do envio: o Lucas escreve junto com o prompt algo como **"você é
> responsável pela TAREFA C"** — e você faz SÓ essa tarefa, respeitando o escopo de
> arquivos dela. Se nenhuma tarefa foi indicada, pergunte antes de começar.

---

## 🗺️ PARTE 0.1 — MAPA DE LOCAIS NO PC (todos os caminhos que importam)

| Local | Caminho completo | O que é |
|---|---|---|
| **Repositório do projeto** | `C:\Users\Lucas\source\repos\zeebo_libretro` | Todo o código-fonte, scripts de build, testes e documentação técnica. É AQUI que se trabalha. |
| **Romset externo (68 jogos)** | `C:\Users\Lucas\Downloads\zeebo-romset-and-devtools` | ROMs comerciais completas do Zeebo (~2.1 GB). Estrutura interna: `Zeebo\Zeebo\Zeebo Game & App Compilation - OpenZeebo\` com pacotes `.7z` por jogo + a pasta `274804\` já extraída (`mif\<id>.mif` e `mod\<id>\...` de vários jogos juntos). IDs Tier 1: `276212` Pac-Mania, `277229` Family Pack, `274754` Double Dragon, `279382` Zeeboids. Para os 4 jogos Tier 1 normalmente NÃO é preciso vir aqui — há cópias dentro do repo em `tests\roms\` (ver abaixo). |
| **RetroArch (cores)** | `C:\Program Files (x86)\Steam\steamapps\common\RetroArch\cores` | Onde a DLL compilada (`zeebo_libretro.dll`) é instalada para teste manual no RetroArch. O script `build_safe.ps1` já instala aqui automaticamente. Logs do RetroArch: `C:\Program Files (x86)\Steam\steamapps\common\RetroArch\logs`. |
| **Vault Obsidian do Lucas** | `G:\Meu Drive\Documentos obi\Projeto\Emulador` | Documentação pessoal consolidada do projeto (planejamento original nos docs `01`-`13`, estado atual em `12_Estado_Atual\`, metodologia em `13_Metodologia_Sessoes\`). **Só leitura para vocês** — quem atualiza é a tarefa F ou o Lucas. Útil como contexto extra, mas a fonte de verdade técnica é o repositório. |

---

## 🧭 PARTE 0.2 — ONBOARDING (leia mesmo se achar que já conhece o projeto)

### O que é este projeto
Um **core libretro/RetroArch** que emula o console **Zeebo** (Brasil/México, 2009).
Diferente de um console normal, o Zeebo roda jogos como **applets do BREW/AEE** (a
plataforma de apps da Qualcomm, tipo J2ME) — não código bare-metal. Este core:
**interpreta CPU ARM/Thumb real** (não é JIT) e **reimplementa em C, do zero, toda a
camada de sistema BREW/AEE via HLE** (o BREW real da Qualcomm não está disponível, então
não tem "pular a BIOS" — o core responde cada chamada de API que o jogo faz).

**Estado hoje**: nenhum jogo é 100% jogável ainda, mas o pipeline inteiro é real:
CPU → memória → loader de ROM → HLE do BREW → GPU/EGL/GLES → framebuffer → RetroArch.
Todos os 4 jogos prioritários têm causa raiz de travamento já diagnosticada (Parte 1) —
esta rodada é majoritariamente de **implementar os fixes já diagnosticados**.

### Mapa de arquivos do repositório (onde está cada coisa)

```
C:\Users\Lucas\source\repos\zeebo_libretro\
├── src\
│   ├── core\            → integração libretro (retro_run, retro_load_game...)
│   │   └── libretro_core.c   ← loop principal, chamado a cada frame pelo RetroArch
│   ├── cpu\              → interpretador ARM/Thumb
│   │   ├── cpu.c              ← fetch-decode-execute, detecção de "CPU descarrilou"
│   │   ├── decode.c            ← barrel shifter compartilhado
│   │   ├── execute_arm.c / execute_thumb.c  ← execução por conjunto de instrução
│   │   └── flags.c             ← cálculo de NZCV
│   ├── memory\            → mapa de memória e heap
│   │   ├── memory.c/h          ← read/write 8/16/32, bounds checking "nunca trava";
│   │   │                          constantes ZMEM_* (RAM 64MB, heap, stack, VRAM)
│   │   └── heap.c              ← alocador MALLOC/FREE/REALLOC do guest
│   ├── loader\             → transforma .mod/.mif em memória pronta pra rodar
│   │   ├── mod_loader.c/h      ← parse do .mod (header BREW ou raw), assets, .sig
│   │   ├── mif_parser.c        ← extrai CLSID do .mif (varre arquivo inteiro + .bar)
│   │   └── bar_parser.c        ← detecção do .bar companheiro
│   ├── brew\                → HLE do BREW/AEE — O SUBSISTEMA MAIS IMPORTANTE
│   │   ├── brew.c/h             ← dispatcher de traps (endereços mágicos 0xF0000xxx),
│   │   │                          enum ZT_* de todos os traps, macro ZTRAP_ID()
│   │   ├── boot.c               ← máquina de estados de boot (AEEMod_Load →
│   │   │                          CreateInstance → EVT_APP_START → rodando),
│   │   │                          zbrew_handle_stub() = os "case" por CLSID,
│   │   │                          zboot_process_timers() = motor de eventos/timers
│   │   ├── helpers.c            ← AEEHelperFuncs (117 slots, memmove/sprintf/etc)
│   │   ├── ishell.c             ← vtable real do IShell (128 slots)
│   │   ├── ifile.c              ← IFile/IFileMgr (sandbox pro diretório da ROM)
│   │   ├── idisplay.c / idisplay_real.c / ibitmap.c  ← IDisplay 2D clássico
│   │   ├── isound.c / imemory.c
│   │   └── aee_ids.h            ← tabela de CLSIDs conhecidos (nome ↔ constante)
│   ├── gpu\                  → framebuffer 2D + EGL/GLES 1.x
│   │   ├── framebuffer.c/h      ← VRAM 640×480 XRGB8888, fill/rect/linha/blit
│   │   ├── draw.c               ← primitivas 2D clássicas
│   │   └── egl_gl.c/h           ← MAIOR ARQUIVO DO PROJETO (~1400 linhas). HLE de
│   │                               AEECLSID_EGL/AEECLSID_GL, rasterizador de software
│   │                               (raster_triangle, transform_vertex, draw_prim,
│   │                               decode_vertex_ptr)
│   ├── audio\                → mixer PCM 44.1kHz estéreo 16 vozes
│   ├── input\                 → RetroPad → bitmask de botões Zeebo
│   └── debug\                  → log.c/h `[Zeebo]`, trace.c (ring de 64 PCs), disasm.c
├── tests\
│   ├── libretro_smoke.c       ← host libretro mínimo p/ testar sem abrir RetroArch
│   │                              (JÁ COMPILADO: tests\libretro_smoke.exe)
│   ├── test_cpu.c / test_memory.c  ← vazios (sem suíte automatizada ainda)
│   └── roms\                   ← cópias das 4 ROMs Tier 1 direto no repo:
│       real_pacmania_game.mod, real_family_pack_game.mod,
│       real_ddragon_game.mod, real_zeeboids_game.mod   ← USEM ESSAS
├── docs\
│   ├── PROGRESS.md              ← timeline de desenvolvimento (histórico)
│   ├── TESTING.md                ← guia de teste completo
│   ├── ROADMAP_50_IDEIAS.md       ← backlog de 50 recursos futuros priorizados
│   ├── THIRD_PARTY.md / PORTABILITY.md / PLANNING_ARCHIVE.md
├── BLOCKERS_ANALYSIS.md         ← bloqueadores tabulares (a Parte 1 deste prompt é mais nova)
├── STATUS_A_EVENTO_DOUBLE_DRAGON.md    ← relatório Rodada 2 (na branch rodada2/double-dragon)
├── STATUS_B_RENDER_FAMILYPACK.md        ← relatório Rodada 2 (na raiz, working tree)
├── STATUS_C_BOOT_PACMANIA_ZEEBOIDS.md    ← relatório Rodada 2 (branch rodada2/pacmania-zeeboids-boot)
├── WORKFLOW_DESENVOLVIMENTO.md   ← guia "testar antes de compilar" (ver Parte 2)
├── test_before_build.ps1 / build_safe.ps1  ← testes pré-build + build que instala no RetroArch
├── quick_build.ps1                ← build rápido sem os testes
├── analyze_clsids.ps1              ← varre o romset externo listando CLSIDs por jogo
├── zeebo_libretro.sln              ← solução Visual Studio 2022 (build principal)
└── README.md                       ← visão geral honesta do projeto
```

### Como compilar e testar (comandos reais)

```powershell
cd C:\Users\Lucas\source\repos\zeebo_libretro   # ou o SEU worktree, ver Parte 1

# Build com testes automáticos antes (recomendado; também instala a DLL no RetroArch):
.\build_safe.ps1
# OU build rápido sem os testes:
.\quick_build.ps1

# Smoke test (sem abrir RetroArch, roda 180 frames e mostra logs):
tests\libretro_smoke.exe x64\Release\zeebo_libretro.dll tests\roms\real_pacmania_game.mod
tests\libretro_smoke.exe x64\Release\zeebo_libretro.dll tests\roms\real_family_pack_game.mod
tests\libretro_smoke.exe x64\Release\zeebo_libretro.dll tests\roms\real_ddragon_game.mod
tests\libretro_smoke.exe x64\Release\zeebo_libretro.dll tests\roms\real_zeeboids_game.mod
```

O que observar na saída (stderr): linhas `[INFO]/[ERROR] [Zeebo] ...` (progresso do
boot), `[VIDEO] 640x480 pitch=... first=0x########` (confirma que `retro_run` produz
frames), e se aparecer `CPU descarrilou: fetch em 0x... (LR=... SP=...)` com dump de
trace de 64 PCs, o boot travou — leia o trace, não ignore.

---

## 📖 PARTE 1 — ESTADO REAL AGORA + REGRAS DE ISOLAMENTO

### Resultado da Rodada 2 (leiam os STATUS completos antes de duplicar investigação)

| Jogo | Estado | Causa raiz identificada? | Corrigido? |
|---|---|---|---|
| **Double Dragon** | Não trava mais (event loop confirmado funcionando). Preso num retry loop legítimo. | ✅ SIM — `case 5` de `zbrew_handle_stub()` (`boot.c`) trata `AEECLSID_DISPLAY_REAL` (CLSID `0x01001001`) pelo `else` genérico, que retorna sucesso mas não escreve os 2 sub-objetos em `r2`/`r3` que o jogo espera. | ❌ NÃO — fix identificado com precisão, não implementado |
| **Family Pack** | Renderiza via `glDrawArrays`, heap pointers agora aceitos em `decode_vertex_ptr`. | Parcial — bug do ponteiro inválido resolvido, mas "ainda depende de mais ajustes de conteúdo/recursos" (`STATUS_B`) | ⚠️ PARCIAL — **e o diff está SEM COMMIT na branch errada, ver aviso crítico abaixo** |
| **Pac-Mania** | Ainda descarrila em `0xFF000000`. | ✅ SIM, matemática de stack exata: `CreateInstance` é chamado num endereço que é **meio de função** (sem prólogo); o epílogo compartilhado estoura o SP em exatos `0x58` bytes, direto pra dentro da VRAM. 2 hipóteses de fix em `STATUS_C`. | ❌ NÃO |
| **Zeeboids** | Roda 1M+ instruções (antes travava imediato). CLSID confirmado byte a byte: `0x0108FF1A`. | ✅ SIM — o stub de relocação PIC (scatter-load `Region$$Table` do `armcc`) assume que a tabela está no offset 0 do módulo, mas o loader coloca o **código do jogo** ali — o stub lê lixo, `r1` vira 0, e a CPU "anda" por RAM zerada até estourar os 64MB. | ❌ NÃO — 2 caminhos em `STATUS_C` |

**Sempre decodifiquem endereços de trap com `ZTRAP_ID(addr)` contra o enum em
`src/brew/brew.h` antes de dizer "API não implementada"** — uma sessão anterior (commit
`72fbf38`) especulou errado sobre isso, e a Rodada 2 corrigiu com evidência real.

### ⚠️ AVISO CRÍTICO — worktree obrigatório (aprendizado da Rodada 2)

Na Rodada 2, as IAs trabalharam **no mesmo diretório físico** usando só `git checkout
-b` pra "isolar". **Não funcionou**: uma IA trocou de branch e isso trocou o branch
ativo por baixo dos pés de outra no meio de uma investigação. Resultado: o fix do
Family Pack (`decode_vertex_ptr` em `src/gpu/egl_gl.c`, ~45 linhas) ficou **sem commit**
no working directory, que ficou parado na branch errada (`rodada2/pacmania-zeeboids-boot`).

**Nesta rodada, com 6 IAs, cada uma DEVE usar `git worktree` — diretório físico próprio:**

```powershell
cd C:\Users\Lucas\source\repos\zeebo_libretro
git status                          # confirme que não vai perder nada pendente
git worktree add ..\zeebo_libretro-<slug> -b rodada3/<slug>
cd ..\zeebo_libretro-<slug>         # daqui em diante, trabalhe SÓ neste diretório
```

Use como `<slug>` o nome da sua tarefa: `double-dragon`, `family-pack`, `pacmania`,
`zeeboids`, `debug-tools`, `validacao`. Regras: commits pequenos com mensagem em pt-BR
(terminando em `Co-Authored-By: <seu nome> <noreply@anthropic.com>`), **nunca** commitar
em `master`, nunca `git push --force`, nunca trocar o branch do diretório principal.

---

## 🔎 PARTE 2 — INVESTIGAR ANTES DE COMPILAR (obrigatório para toda tarefa)

O padrão de qualidade é o de `STATUS_C_BOOT_PACMANIA_ZEEBOIDS.md`: causa raiz confirmada
por **matemática exata** (aritmética de SP, deltas PC-relative) e **desmontagem real**
(capstone), não por suposição. Antes de mudar qualquer linha:

1. Leiam o `STATUS_*.md` relevante à sua tarefa até o fim.
2. Rodem o smoke test do jogo **antes** de mudar nada — essa é a sua baseline.
3. Se a causa raiz não estiver 100% clara, desmontem o trecho relevante do `.mod` com
   `capstone` (Python: `pip install capstone`), ou adicionem `LOGE`/`LOGD` temporários
   (revertidos antes do commit final) para confirmar valores reais de
   registrador/memória em vez de adivinhar.
4. Só com hipótese + evidência (não "acho que é isso"), editem o código.
5. Depois de editar: `.\test_before_build.ps1` → `.\build_safe.ps1` → smoke test de
   novo, comparando com a baseline do passo 2.
6. Nunca reportem "✅ implementado" sem colar o log do smoke test **depois** do fix.
7. Antes de encerrar, rodem o smoke test também nos OUTROS 3 jogos Tier 1 — se sua
   mudança regrediu algum jogo que antes ia mais longe, isso é resultado importante,
   reportem (não escondam).

---

## 🅰️ TAREFA A — Double Dragon: implementar o fix do `case 5`

**Worktree/branch:** `rodada3/double-dragon`
**Arquivo de saída:** `STATUS_A2_DOUBLE_DRAGON_FIX.md`
**Escopo de código:** `src/brew/boot.c`, função `zbrew_handle_stub()`, `case 5` (por
volta das linhas 930-962) — **leiam como o `case` do CLSID `0x0100101Cu` (Pac-Mania) já
resolve isso** (aloca 2 objetos via `make_stub_interface()` e escreve nos out-params)
antes de mexer; é o padrão a seguir.

1. Baseline: smoke test contra `tests\roms\real_ddragon_game.mod`, log colado no status.
2. Leiam `STATUS_A_EVENTO_DOUBLE_DRAGON.md` (branch `rodada2/double-dragon`:
   `git show rodada2/double-dragon:STATUS_A_EVENTO_DOUBLE_DRAGON.md`) — já tem o
   diagnóstico exato do loop de retry e do CLSID `0x01001001`.
3. No `case 5`, tratem `AEECLSID_DISPLAY_REAL` (`0x01001001`) do mesmo jeito que
   `0x0100101Cu` já é tratado. Se não tiverem certeza da semântica exata desse CLSID,
   desmontem o trecho do `.mod` que consome o retorno (metodologia do `STATUS_C`) antes
   de assumir que é idêntico.
4. Rebuild + smoke test: o retry loop parou? O jogo avança pra outro estado? Aparecem
   `glDrawArrays`/`glClear` reais?
5. Regressão: rodem os outros 3 jogos (o `case 5` é genérico — mudança ali afeta todos).

**Reportar:** diff, log antes/depois, resposta objetiva (avançou até onde? novo ponto de
travamento com PC/LR/SP/trap decodificado, se ainda travar).

---

## 🅱️ TAREFA B — Family Pack: resgatar o fix perdido + terminar o render

**Worktree/branch:** `rodada3/family-pack`
**Arquivo de saída:** `STATUS_B2_RENDER_FAMILYPACK.md`
**Escopo de código:** `src/gpu/egl_gl.c` (vertex arrays/rasterização)

1. **RESGATE (primeira coisa, antes de criar o worktree):** no diretório principal
   (`C:\Users\Lucas\source\repos\zeebo_libretro`), rodem `git status` e
   `git diff src/gpu/egl_gl.c`. Confirmem que o diff pendente é o fix descrito em
   `STATUS_B_RENDER_FAMILYPACK.md` (decode_vertex_ptr aceitando RAM/heap/stack/VRAM).
   Commitem esse diff na branch `rodada2/family-pack-render` (ex.: `git stash` →
   `git checkout rodada2/family-pack-render` → `git stash pop` → commit), depois voltem
   o diretório principal pra branch em que estava. Só então criem seu worktree
   `rodada3/family-pack` **a partir de `rodada2/family-pack-render`**:
   `git worktree add ..\zeebo_libretro-family-pack -b rodada3/family-pack rodada2/family-pack-render`
2. Baseline: smoke test contra `tests\roms\real_family_pack_game.mod`; confirmem que
   bate com o que `STATUS_B` reportou (heap pointers aceitos, `DrawArrays` ativo).
3. `STATUS_B` deixou em aberto: "o frame ainda depende de mais ajustes de
   conteúdo/recursos". Investiguem o que falta de verdade: textura não carregada?
   array de cor/vértice com dado errado? Usem `glReadPixels` (já implementado) ou dump
   de pixels pra ver o que está NO framebuffer, não o que deveria estar.
4. Meta: um frame reconhecível como menu do Family Pack (não cor sólida).

**Reportar:** hash do commit de resgate, o que faltava e o que foi corrigido, evidência
do conteúdo do framebuffer (valores reais de `glReadPixels`/coordenadas dos triângulos).

---

## 🅲️ TAREFA C — Pac-Mania: consertar a chamada de `CreateInstance`

**Worktree/branch:** `rodada3/pacmania`
**Arquivo de saída:** `STATUS_C2_PACMANIA_FIX.md`
**Escopo de código:** `src/brew/boot.c` (seção `IModule_CreateInstance` /
`zboot_on_guest_return`), possivelmente `src/cpu/cpu.c` (SP inicial) — NÃO mexam no
`case 5` do stub (escopo da Tarefa A).

Leiam a seção PAC-MANIA de `STATUS_C_BOOT_PACMANIA_ZEEBOIDS.md` inteira — o diagnóstico
já está pronto (desmontagem + matemática de stack). Implementem uma das 2 hipóteses:

- **Opção 1 (correção real, preferida):** a convenção assumida em `boot.c`
  ("vtable+8 = CreateInstance") pode estar errada pra esse binário — as 4 "vtable
  slots" parecem um dispatcher único fatiado pelo compilador (RVCT), possivelmente
  exigindo frame de pilha já montado pelo chamador. Desmontem mais contexto em volta de
  `0x10DC`/`0x1108`/`0x1104` com capstone e confirmem qual é o entry point real com
  prólogo — pode ser que o caminho correto seja usar `pfnModCrInst` (offset +12 do
  AEEMod) ou outro slot.
- **Opção 2 (mitigação, só se a 1 esgotar):** colchão de `0x58+` bytes entre o SP
  inicial de `cpu_reset` e o topo real da stack, para overruns deste tipo caírem em
  stack real, não em VRAM. Se fizerem isso, loguem sempre que o colchão for usado e
  deixem claro no relatório que é mitigação, não correção (o comentário em
  `src/cpu/cpu.c:73-79` avisa que mascarar já causou confusão antes).

Baseline antes / smoke depois / regressão nos outros 3 jogos.

**Reportar:** opção escolhida e por quê, diff, trace antes/depois, até onde o
Pac-Mania chega agora (antes: descarrilava logo após "CreateInstance case 5 OK").

---

## 🅳️ TAREFA D — Zeeboids: resolver o scatter-load (relocação PIC)

**Worktree/branch:** `rodada3/zeeboids`
**Arquivo de saída:** `STATUS_D2_ZEEBOIDS_FIX.md`
**Escopo de código:** `src/loader/mod_loader.c`, possivelmente `src/loader/bar_parser.c`
e a detecção do bootstrap em `src/brew/boot.c` — NÃO mexam no `case 5` (Tarefa A) nem
na seção CreateInstance (Tarefa C).

Leiam a seção ZEEBOIDS de `STATUS_C_BOOT_PACMANIA_ZEEBOIDS.md` inteira. Implementem um
dos 2 caminhos já propostos lá:

- **Caminho 1:** investigar o `.bar` companheiro (7MB+, hoje ignorado com "magic
  desconhecido") — pode conter a `Region$$Table`/dados de scatter-load reais. Comecem
  com um hexdump dos primeiros KB do `.bar` do Zeeboids (romset externo,
  `274804\mod\279382\` ou junto do `.mif`) pra identificar o formato.
- **Caminho 2 (recomendado pelo `STATUS_C`, mais barato):** detectar o padrão de
  bootstrap PIC genérico do `armcc` na entrada do módulo (`0x40`/`0x48`, idêntico ao do
  Pac-Mania byte a byte) e **pular o stub via HLE**: simular o efeito esperado do
  scatter-load (BSS já é zerado pelo loader — verificar se dados RW também precisam de
  cópia/relocação) e saltar direto pro código pós-bootstrap. Cuidado pra não quebrar o
  Pac-Mania, que executa o mesmo stub sem sofrer (é 5x menor e quase não tem data/bss).

Baseline antes / smoke depois / regressão nos outros 3 jogos (especialmente Pac-Mania,
que compartilha o mesmo bootstrap).

**Reportar:** caminho escolhido, diff, log antes/depois (antes: 1M+ instruções e
descarrilamento em `0x04000000` andando de 4 em 4 bytes), até onde chega agora.

---

## 🅴️ TAREFA E — Ferramentas de debug (acelera todas as outras)

**Worktree/branch:** `rodada3/debug-tools`
**Arquivo de saída:** `STATUS_E_DEBUG_TOOLS.md`
**Escopo de código:** `src/debug/` (log.c/h, trace.c), pontos de hook mínimos em
`src/cpu/cpu.c`, `src/memory/memory.c`, `src/core/libretro_core.c` — mudanças devem ser
**aditivas e desligadas por padrão** (nenhuma mudança de comportamento quando as flags
estão off), pra não conflitar com as tarefas A-D que mexem em lógica.

Implementem, nesta ordem de prioridade (itens 46-50 de `docs/ROADMAP_50_IDEIAS.md`):

1. **Log por categoria:** prefixos `[Zeebo:BOOT]`, `[Zeebo:GPU]`, `[Zeebo:CPU]`,
   `[Zeebo:LOADER]`... com filtro por variável de ambiente (ex.: `ZEEBO_LOG=GPU,BOOT`).
   Migrar só os call sites principais, não os ~centenas de uma vez.
2. **Dump de VRAM em arquivo:** flag (env var) que salva o framebuffer como BMP a cada N
   frames em uma pasta (`vram_dumps\frame_0060.bmp`) — permite "ver" o que o jogo
   desenhou sem abrir o RetroArch. BMP é trivial de escrever sem lib externa.
3. **Watchpoint por endereço de memória:** env var `ZEEBO_WATCH=0x30000048` que loga
   (com PC atual + dump dos registradores) toda escrita/leitura naquele endereço — teria
   acelerado muito o diagnóstico do Pac-Mania.
4. **Contador de instruções por região:** contagem de instruções executadas por faixa
   (código do módulo / heap / stack / fora de tudo), impressa a cada 60 frames — expõe
   loops degenerados como o do Zeeboids imediatamente.
5. **(Opcional) Modo tolerante a crash:** flag explícita que, num fetch inválido, loga e
   tenta continuar em vez de parar — SÓ para diagnóstico exploratório, off por padrão,
   com aviso barulhento no log quando ativa (o projeto documenta deliberadamente "não
   finge que funciona").

Validem cada ferramenta usando um dos travamentos reais conhecidos (ex.: reproduzam o
watchpoint no descarrilamento do Pac-Mania e mostrem o log capturando o momento exato).

**Reportar:** quais das 5 entraram, como ativar cada uma (env vars), e uma demonstração
real de cada uma contra um jogo Tier 1.

---

## 🅵️ TAREFA F — Validação, integração e consolidação

**Worktree/branch:** `rodada3/validacao`
**Arquivo de saída:** `RODADA_3_CONSOLIDADO.md`
**Escopo:** leitura de tudo, escrita só de documentação e scripts — NÃO mexe em `src/`.

Esta tarefa roda em paralelo no começo e vira o "juiz" no final:

1. **Enquanto A-E trabalham:** rodem `analyze_clsids.ps1` contra o romset externo
   completo (`C:\Users\Lucas\Downloads\zeebo-romset-and-devtools\...\274804\`) e
   registrem quantos dos 68 jogos têm CLSID resolvível hoje. Depois, testem por smoke
   test 5-10 jogos FORA do Tier 1 (Crash Bandicoot `274214`, Quake `274802`, Quake II
   `276153`, FIFA 09 `274803`, Resident Evil 4 `276675`...) e classifiquem cada um:
   até onde o boot chega, onde trava, com que erro. Isso alimenta o Tier 2.
2. **Quando A-D terminarem:** criem um worktree de integração, façam merge das branches
   `rodada3/*` que tiverem resultado positivo (na ordem: B → A → C → D → E), resolvam
   conflitos simples (se um conflito for de lógica, devolvam pra IA da tarefa), rebuild,
   e rodem o smoke test dos 4 jogos Tier 1 no resultado integrado.
3. **Consolidem em `RODADA_3_CONSOLIDADO.md`:** a tabela de estado por jogo da Parte 1
   atualizada (coluna "Corrigido?" com evidência), o resultado da varredura dos 68
   jogos, e o "próximo passo #1" mais valioso pra Rodada 4.
4. Se tiverem acesso de escrita ao vault Obsidian
   (`G:\Meu Drive\Documentos obi\Projeto\Emulador\12_Estado_Atual\`), atualizem
   `00_STATUS_ATUAL.md` e `02_HISTORICO_PROGRESSO.md` com o resultado da rodada; se
   não, incluam no consolidado uma seção "para o Lucas colar no Obsidian".

**Reportar:** tudo acima no `RODADA_3_CONSOLIDADO.md` — é o único arquivo que o Lucas
vai ler primeiro, então comecem com um resumo de 10 linhas em linguagem simples (o
Lucas não é programador): quais jogos melhoraram, o que dá pra ver na tela agora, o que
falta.

---

## 🔄 SINCRONIZAÇÃO (todas as tarefas)

1. Cada IA escreve um resumo de até 8 linhas no topo do seu arquivo de status: o que
   mudou, se compila, se foi testado de verdade (log colado), resultado real (não
   intenção). **Evidência > afirmação** — a Rodada 1 teve "✅ IMPLEMENTADO" que era só
   "escrito, não commitado, não testado"; a Rodada 2 corrigiu isso, mantenham o padrão.
2. A Tarefa F integra e consolida (ver acima). As demais **não fazem merge pra
   `master`** — isso é decisão do Lucas depois de testar no RetroArch.
3. Ao terminar, cada IA remove o próprio worktree (`git worktree remove
   ..\zeebo_libretro-<slug>`) — a branch fica, só o diretório físico sai.
