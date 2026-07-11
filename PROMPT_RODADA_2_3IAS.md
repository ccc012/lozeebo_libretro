# 🎯 PROMPT — Rodada 2: 3 IAs em Paralelo (zeebo_libretro)

> Cole este arquivo inteiro na conversa de cada uma das 3 IAs. Só edite o campo
> **Responsável:** de cada tarefa se quiser trocar quem faz o quê — o resto não muda.
> Todo o trabalho é sobre o mesmo repositório:
> `C:\Users\Lucas\source\repos\zeebo_libretro`

---

## 📖 RESUMO (leia isto primeiro, todo mundo)

**Projeto**: core libretro/RetroArch que emula o console Zeebo — CPU ARM interpretada +
HLE completo do sistema BREW/AEE em C. Ainda nenhum jogo é jogável, mas o pipeline
inteiro (CPU → memória → HLE do BREW → GPU/EGL/GLES → framebuffer) já é real e
funcional. Objetivo desta sessão: **sair do "boot avança e trava" para "pelo menos 1
jogo desenha um frame de gameplay reconhecível na tela"**.

**Por que uma Rodada 2**: já rodou uma Rodada 1 de 3 IAs em paralelo neste mesmo repo
(ver `COLLABORATION.md`, `ANALYSIS_FINDINGS.md`, `RENDERING_ANALYSIS.md`,
`TESTS_MOD_ZIP.md`, `FIXES_TO_IMPLEMENT.md`, `IMPLEMENTATION_LOG.md` — **leia esses
arquivos antes de começar**, eles têm contexto real já levantado). Um teste real contra
os 4 jogos prioritários (commit `9460e42`, "Test 4 Zeebo games: Double Dragon most
advanced") deu este ranking, que é o ponto de partida real desta rodada:

| Jogo | CLSID | Onde trava hoje |
|---|---|---|
| **Double Dragon** | `0x0102F789` | Chega a `EVT_APP_START` → estado "RODANDO" — **o mais avançado**, depois trava |
| **Family Pack** | `0x010903C6` | Renderiza via `glDrawArrays` de verdade, mas geometria some da tela (vertex pointer `0x00000003`) |
| **Pac-Mania** | `0x01087B72` | CPU descarrila em `0xFF000000` depois do `CreateInstance` |
| **Zeeboids** | desconhecido | Trava dentro do próprio `AEEMod_Load`, nem chega no `CreateInstance` |

**⚠️ Correção importante sobre uma suposição da Rodada 1**: o commit `72fbf38` supôs que
Double Dragon travava num "trap HLE não implementado `0xF0000A40`". **Isso é falso** —
já verifiquei: `0xF0000A40 - 0xF0000000 = 0xA40`, `0xA40 >> 2 = 0x290`, e `0x290` é
`ZT_GUEST_RETURN` em `src/brew/brew.h` (o trap **normal** de "a chamada terminou, avance
a máquina de estados"). Ou seja, esse endereço não é uma API faltando — é o retorno de
guest chamada acontecendo normalmente. **A causa real do travamento do Double Dragon
ainda não foi encontrada.** Não repitam esse erro: sempre decodifiquem um endereço de
trap com `ZTRAP_ID(addr)` (`src/brew/brew.h`) e confiram contra o enum antes de concluir
"API não implementada".

**Estado do working tree AGORA (não commitado ainda)**: `git status` no repositório
mostra mudanças pendentes em `src/brew/boot.c`, `src/brew/brew.h`, `src/brew/aee_ids.h`,
`src/core/libretro_core.c`, `src/gpu/egl_gl.c`, `src/loader/mif_parser.c` — inclui a
função `zboot_process_timers()` já escrita e já chamada em `retro_run()`
(`src/core/libretro_core.c`, dentro de `retro_run`), e um `decode_vertex_ptr()` mais
defensivo em `egl_gl.c`. **Ninguém commitou isso ainda e ninguém testou contra o
RetroArch de verdade** — só por smoke test parcial. Rodem `git diff` no início da sessão
para ver exatamente o que está pendente antes de mexer em qualquer coisa.

---

## 🔒 REGRAS PARA TRABALHAR EM PARALELO NO MESMO REPO (leiam antes de editar)

1. **Isolem-se em branch própria.** Antes de editar qualquer arquivo, rodem:
   ```powershell
   git checkout -b rodada2/<slug-da-sua-tarefa>
   ```
   (ex.: `rodada2/double-dragon`, `rodada2/family-pack-render`,
   `rodada2/pacmania-zeeboids-boot`). Isso evita que as 3 IAs editando o mesmo
   `src/brew/boot.c` ao mesmo tempo se pisem sem ninguém perceber — foi o que
   provavelmente causou confusão na Rodada 1 (o `IMPLEMENTATION_LOG.md` já descreve como
   "implementado" algo que o `git log` mostra como pendente de commit).
2. **Nunca marquem algo como "✅ resolvido" ou "✅ implementado" sem colar o log real**
   de um smoke test ou execução no RetroArch mostrando o resultado. "Compilou sem erro"
   não é "funciona". A Rodada 1 caiu nessa armadilha mais de uma vez.
3. **Cada tarefa tem um arquivo de saída próprio** (ver cada seção abaixo) — não editem
   o arquivo de saída de outra tarefa.
4. **Façam commits pequenos e frequentes na sua branch**, com mensagem clara em
   português, terminando com `Co-Authored-By: <seu nome> <noreply@anthropic.com>` (ou
   equivalente). Não deem `git push --force`, não mexam em `master` diretamente.
5. **Responda tudo em português.** Commits, comentários novos no código e os arquivos
   de status pedidos abaixo — tudo em pt-BR.
6. **Compilem e rodem o smoke test antes de reportar qualquer resultado**:
   ```powershell
   .\quick_build.ps1
   # ou: cmake --build --preset desktop-smoke
   tests\libretro_smoke.exe x64\Release\zeebo_libretro.dll `
       "C:\Users\Lucas\Downloads\zeebo-romset-and-devtools\Zeebo\Zeebo\Zeebo Game & App Compilation - OpenZeebo\274804\mod\<ID>\<arquivo>.mod"
   ```
   IDs: Double Dragon `274754`, Family Pack `277229`, Pac-Mania `276212`, Zeeboids `279382`.
7. **Se encontrarem uma suposição de sessão anterior que parece errada** (como a do item
   acima), verifiquem com evidência antes de descartar OU confirmar — e registrem a
   verificação no arquivo de saída, não só a conclusão.

---

## 🅰️ TAREFA A — Motor de eventos + Double Dragon (maior chance de sucesso rápido)

**Responsável:** _[preencher nome/IA aqui]_
**Branch:** `rodada2/double-dragon`
**Arquivo de saída:** `STATUS_A_EVENTO_DOUBLE_DRAGON.md`
**Arquivos de código no seu escopo:** `src/brew/boot.c` (funções de timer/estado, NÃO
mexer nas seções de `IModule_CreateInstance`/case 5 — isso é da Tarefa C),
`src/core/libretro_core.c` (`retro_run`), `src/brew/brew.h`.

### Passo a passo
1. `git diff src/brew/boot.c src/core/libretro_core.c` — leiam a mudança pendente de
   `zboot_process_timers()` e onde ela é chamada em `retro_run()`. Entendam a lógica:
   ela só zera `g_cpu.halted` quando **nenhum** timer está ativo; se há timer ativo mas
   não expirado, a CPU continua parada.
2. Compilem o estado atual (com a mudança pendente) e rodem o smoke test contra
   **Double Dragon** (`274754`). Colem a saída completa (stderr) no arquivo de status.
3. Nos logs, procurem a sequência: `boot: EVT_APP_START tratado`, depois qualquer
   `IShell_SetTimer`/`SetTimer`, depois `boot: timer %d expirou`. Determinem:
   - O jogo chama `SetTimer` depois de `EVT_APP_START`? Se não, o problema não é timer.
   - Se chama, o timer expira e o callback é chamado (`guest_call`)? Rastreiem até onde
     o PC vai depois disso.
   - Se a CPU trava de novo (halted=true permanece), qual trap/estado causou isso?
     **Decodifiquem qualquer endereço `0xF0000xxx` com `ZTRAP_ID()` contra o enum em
     `src/brew/brew.h` antes de concluir "API não implementada"** — não repitam o erro
     do commit `72fbf38`.
4. Se acharem uma API/trap genuinamente não implementada (handler cai no `default` de
   `trap_dispatch` em `src/brew/brew.c`), implementem o comportamento mínimo plausível
   (retornar sucesso/0, ou o comportamento real se for óbvio pelo padrão dos outros
   handlers) e testem de novo.
5. Se o event loop em si parecer correto mas o jogo ainda travar num ponto específico,
   documentem exatamente **onde** (PC, LR, SP, trace de 64 PCs) — não precisam
   resolver tudo, só deixar o próximo passo claro.
6. Meta-alvo (se tudo alinhar): Double Dragon roda mais de 1 ciclo de `SetTimer` sem
   travar, e o smoke test mostra `[VIDEO]` avançando com conteúdo mudando entre frames
   (não só o primeiro).

### O que reportar em `STATUS_A_EVENTO_DOUBLE_DRAGON.md`
- Diff real aplicado (ou "nenhuma mudança de código, só diagnóstico").
- Log real do smoke test (trecho relevante, não o log inteiro).
- Resposta objetiva: **o event loop funciona hoje? SIM/NÃO, com evidência.**
- Onde Double Dragon trava agora (se ainda travar) — PC/LR/SP/trap decodificado.
- Se resolvido: quantos frames rodou sem travar, o que aparece no framebuffer.

---

## 🅱️ TAREFA B — Renderização visível: Family Pack (vertex pointer + cor)

**Responsável:** _[preencher nome/IA aqui]_
**Branch:** `rodada2/family-pack-render`
**Arquivo de saída:** `STATUS_B_RENDER_FAMILYPACK.md`
**Arquivos de código no seu escopo:** `src/gpu/egl_gl.c` (só as seções de vertex
array/rasterização — não mexam nos handlers de `IShell`/boot em outros arquivos).

### Contexto técnico já levantado (não refaçam do zero)
- `decode_vertex_ptr()` (`src/gpu/egl_gl.c`, por volta da linha 760) já trata o caso do
  ponteiro vir pequeno/na stack (convenção Qualcomm: `glVertexPointer` às vezes recebe o
  endereço real em `stack[0]` em vez de R3) — mas se **nem o valor bruto nem o
  `stack[0]` forem endereços válidos**, ela retorna `0` e o triângulo é descartado
  silenciosamente. Isso pode estar mascarando o bug real em vez de corrigi-lo.
- `GLFN_ColorPointer` (por volta da linha 1070) já faz
  `g_va_col.on = (ptr != 0)` — ou seja, já auto-habilita o array de cor quando o
  ponteiro é válido. A Rodada 1 sugeriu isso como fix pendente; **verifiquem se já está
  aplicado e funcionando, ou se ainda falta algo**.
- `g_cur_color` (linha ~347) tem default branco `{1,1,1,1}`. Se o jogo nunca chama
  `glColor4x` nem `glColorPointer`, tudo renderiza branco puro — isso é comportamento
  esperado do OpenGL ES real (branco É o default do spec), então **não tratem isso como
  bug a menos que constatem que o jogo deveria estar mandando cor e não está chegando**.

### Passo a passo
1. `git diff src/gpu/egl_gl.c` — vejam o que já está pendente de commit.
2. Compilem, rodem o smoke test contra **Family Pack** (`277229`), ativem/leiam os logs
   `glVertexPointer(...)` e `VP INVALID ptr=...` (já existem no código, procurem por
   `LOGD`/`LOGW` em `decode_vertex_ptr` e no case `GLFN_VertexPointer`).
3. Respondam: quando o ponteiro chega como `0x00000003` (ou outro valor pequeno
   inválido), o que tem em `stack[0]` (`zbrew_stack_arg(0)`) naquele exato momento? É um
   endereço válido de RAM que a heurística atual está rejeitando por engano, ou é lixo
   de verdade?
   - Se for um endereço válido rejeitado por engano: o critério de validação
     (`< 0x04000000u`, `>= ZMEM_STACK_BASE` etc.) está errado — ajustem.
   - Se for lixo de verdade: o bug é **antes** dessa função, na convenção de chamada de
     `zgl_handle()`/`zgl_dispatch()` (ver histórico da convenção "sem `this`" descoberta
     na sessão de 2026-07-08 — pode haver um caso similar ainda não coberto, ex. um
     wrapper intermediário deslocando os argumentos de novo).
4. Depois de entender a causa, apliquem um fix real (não só um fallback mais tolerante)
   e testem de novo: os triângulos aparecem dentro da área 640×480, com coordenadas que
   fazem sentido pro layout de um menu 2D?
5. Se sobrar tempo: verifiquem se `glColorPointer`/`glColor4x` estão de fato sendo
   chamados pelo jogo (log `glColorPointer: ...`) — se nunca aparecem nos logs, o
   "branco" é esperado e não é bug de vocês; documentem isso para não virar trabalho
   perdido de outra sessão.

### O que reportar em `STATUS_B_RENDER_FAMILYPACK.md`
- Diff real aplicado.
- Valor de `stack[0]` no momento do `VP INVALID`, com log colado.
- Causa raiz identificada (endereço válido rejeitado vs. lixo genuíno vs. convenção de
  chamada errada em outro lugar).
- Coordenadas de tela antes/depois do fix (números reais dos logs, não estimativa).
- Screenshot ou descrição textual do frame final se possível (via `glReadPixels`/smoke
  test), confirmando se a geometria ficou visível dentro de 640×480.

---

## 🅲️ TAREFA C — Boot travado: Pac-Mania (SP→VRAM) e Zeeboids (freeze no AEEMod_Load)

**Responsável:** _[preencher nome/IA aqui]_
**Branch:** `rodada2/pacmania-zeeboids-boot`
**Arquivo de saída:** `STATUS_C_BOOT_PACMANIA_ZEEBOIDS.md`
**Arquivos de código no seu escopo:** `src/brew/boot.c` (só as seções de
`IModule_CreateInstance` / `zbrew_handle_stub` case 5, por volta das linhas 930-960 —
NÃO mexam nas funções de timer, isso é da Tarefa A), `src/loader/mod_loader.c`,
`src/loader/mif_parser.c`.

### Contexto técnico já levantado
- **Pac-Mania**: o `case 5` de `zbrew_handle_stub()` (`src/brew/boot.c`, CLSID
  `0x0100101Cu`, por volta da linha 930-954) já foi "corrigido" numa sessão anterior
  para alocar stubs reais via `make_stub_interface()` em vez de escrever `NULL`. **Mas o
  teste real mais recente (commit `9460e42`) mostra que a CPU ainda descarrila em
  `0xFF000000` depois do `CreateInstance`** — ou seja, esse fix não resolveu o problema
  de verdade, ou resolveu só parte dele e há uma segunda causa. `0xFF000000` é a cor de
  limpeza da VRAM (`zfb_clear`) sendo lida como se fosse um ponteiro/endereço de branch
  — sinal de que algum ponteiro na cadeia de chamada ainda está apontando para dentro da
  VRAM (`0x30000000`-`0x301FFFFF`) em vez da stack real (`0x2FC00000`-`0x2FFFFFFF`).
- **Zeeboids**: CLSID ainda desconhecido, trava dentro do próprio `AEEMod_Load` — mais
  cedo até que o Pac-Mania. Há uma melhoria pendente/uncommitted em
  `src/loader/mif_parser.c` (`scan_file_for_clsid()`) que passou a varrer o `.mif`
  inteiro (não só os primeiros 8KB) e também o `.bar` companheiro — **ainda não foi
  testada contra o Zeeboids de verdade**. Também existe `analyze_clsids.ps1` na raiz do
  repo, que varre o romset externo listando CLSIDs por jogo — pode ajudar a achar o
  CLSID certo do Zeeboids mais rápido que ler o `.mod` binário à mão.

### Passo a passo — Pac-Mania
1. `git diff src/brew/boot.c` na seção do case 5 — confirmem que o fix mencionado acima
   já está no working tree (não precisa refazer).
2. Compilem, rodem smoke test contra **Pac-Mania** (`276212`), capturem o trace de 64
   PCs completo do `CPU descarrilou`.
3. Com o trace em mãos, identifiquem o PC exatamente ANTES de `0xFF000000` aparecer, e
   qual registrador (SP, ou outro) carrega esse valor. Rastreiem de onde esse valor
   veio: é lido de uma vtable? De um out-param de alguma outra `case` do
   `zbrew_handle_stub()` que ainda escreve incorretamente? Comparem com o padrão
   correto usado no `case 3` (`SignalCBFactory`), que já funciona.
4. Apliquem o fix real na causa encontrada (pode ser em outro `case`, não
   necessariamente no 5) e re-testem até o `CreateInstance` completar sem descarrilar
   OU até vocês terem uma explicação clara e documentada do que ainda falta.

### Passo a passo — Zeeboids
1. Rodem `analyze_clsids.ps1` (ou leiam `279382.mif` diretamente) para achar o CLSID
   real do Zeeboids — hoje é "desconhecido" na tabela de status do projeto.
2. Compilem com a melhoria pendente do `mif_parser.c` já aplicada, rodem o smoke test
   contra **Zeeboids** (`279382`), e confirmem se o CLSID agora é resolvido
   automaticamente (log de `mif: applet '...' (0x...) em ...`) ou se ainda cai no
   fallback genérico.
3. Se o CLSID for resolvido: onde exatamente o `AEEMod_Load` trava agora? PC/LR/SP/trace.
4. Se ainda não for resolvido: abram o `.mod` (`279382/*.mod`) e procurem manualmente
   por constantes de 4 bytes que pareçam CLSID (ver o padrão usado para os outros 3
   jogos em `src/brew/aee_ids.h`) — documentem o valor encontrado mesmo que não dê tempo
   de implementar o resto.

### O que reportar em `STATUS_C_BOOT_PACMANIA_ZEEBOIDS.md`
- Diff real aplicado, separado por jogo.
- Trace completo do Pac-Mania (colado, não resumido) com a instrução/registrador
  identificado como origem do `0xFF000000`.
- CLSID real do Zeeboids (se encontrado) e como foi encontrado.
- Novo ponto de travamento do Zeeboids (se avançou) com PC/LR/SP/trace.

---

## 🔄 SINCRONIZAÇÃO (depois que as 3 tarefas terminarem)

1. Cada responsável escreve um resumo de **no máximo 8 linhas** no topo do próprio
   arquivo de status: o que mudou, se compila, se foi testado de verdade (smoke test
   e/ou RetroArch), resultado real (não intenção).
2. Uma das 3 IAs (ou uma quarta passada) consolida os três `STATUS_*.md` em
   `RODADA_2_CONSOLIDADO.md`, no mesmo formato de `FIXES_TO_IMPLEMENT.md` da Rodada 1:
   tabela de bloqueadores, o que foi resolvido de verdade (com evidência), o que ainda
   falta, e qual é o próximo passo #1 mais valioso.
3. As 3 branches (`rodada2/double-dragon`, `rodada2/family-pack-render`,
   `rodada2/pacmania-zeeboids-boot`) ficam prontas para revisão/merge por Lucas — **não
   deem merge para `master` sozinhas**, isso é decisão do Lucas.
4. Lucas testa manualmente no RetroArch (não só smoke test) os 4 jogos com o build de
   cada branch (ou uma branch combinada, se ele decidir mesclar antes) e atualiza
   `docs/TESTING.md` / o vault do Obsidian com o resultado real.

---

## 💡 Ideias extras para essa rodada sair melhor que a anterior

- **Evidência > afirmação.** Nenhum status deve dizer só "funciona" — sempre com log
  colado ou trace colado. A Rodada 1 teve pelo menos um caso de "✅ IMPLEMENTADO" que na
  verdade era só "escrito, não commitado, não testado".
- **Decodifiquem endereços de trap antes de especular.** Use `ZTRAP_ID(addr)` contra o
  enum de `src/brew/brew.h` sempre que um log mostrar um endereço `0xF0000xxx`.
  Endereços fora da janela `0xF0000000`-`0xF0001000`, ou dentro da faixa de VRAM
  (`0x30000000`-`0x301FFFFF`), são sinal de ponteiro corrompido, não de trap.
- **Branch isolada de verdade**, não só "cuidado ao editar" — reduz a chance de duas IAs
  sobrescreverem a mesma mudança sem perceber, que é o que provavelmente gerou a
  confusão entre `IMPLEMENTATION_LOG.md` dizer "implementado" e o `git log` mostrar
  "planejado para depois" na Rodada 1.
- **Timebox realista**: cada tarefa acima é mais profunda que as da Rodada 1 (que eram
  de 10-15 min de leitura de código). Estimem 30-45 min de trabalho de verdade por
  tarefa, não 10 min — travamentos de CPU real exigem ler trace, não só grep.
