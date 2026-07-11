# 🎯 PROMPT — Rodada 6: 10 IAs em Paralelo (zeebo_libretro)

> Cole este arquivo inteiro na conversa de cada IA. A atribuição vem na hora do envio:
> o Lucas escreve **"você é responsável pela TAREFA D"** junto com o prompt. Faça SÓ a
> sua tarefa, dentro do escopo de arquivos dela. Sem tarefa indicada? Pergunte antes.
> Responda tudo em pt-BR.
>
> **Nota pro Lucas na hora de distribuir**: as tarefas **A** e **F** são as mais
> difíceis (engenharia reversa real). Se tiver um modelo mais forte disponível, mande
> A e F pra ele. B, C, D, H e I são as mais mecânicas — perfeitas pros modelos baratos.

---

## 🗺️ LOCAIS NO PC

| Local | Caminho | O que é |
|---|---|---|
| **Repositório** | `C:\Users\Lucas\source\repos\zeebo_libretro` | Código, scripts, testes, docs. Branch `master` já contém TODO o trabalho das rodadas 1-5 integrado. |
| **ROMs Tier 1** | `...\zeebo_libretro\tests\roms\` | `real_pacmania_game.mod`, `real_family_pack_game.mod`, `real_ddragon_game.mod`, `real_zeeboids_game.mod` — já no repo. |
| **Romset (62 jogos testáveis)** | `C:\Users\Lucas\Downloads\zeebo-romset-and-devtools\Zeebo\Zeebo\Zeebo Game & App Compilation - OpenZeebo\274804\` | `mif\<id>.mif` e `mod\<id>\<jogo>.mod`. Necessário pras tarefas A-D, H. |
| **RetroArch** | `C:\Program Files (x86)\Steam\steamapps\common\RetroArch\cores` | Onde a DLL é instalada. Logs em `...\RetroArch\logs`. |
| **Vault Obsidian** | `G:\Meu Drive\Documentos obi\Projeto\Emulador` | Docs pessoais do Lucas. Só leitura (a Tarefa J atualiza no fim). |

## 🧭 ONBOARDING EM 1 PARÁGRAFO

Core libretro que emula o **Zeebo** (2009): jogos são applets **BREW** em ARM; o core
interpreta a CPU ARM/Thumb e reimplementa a camada BREW inteira via HLE (traps em
`0xF0000000+`; enum `ZT_*` e macro `ZTRAP_ID()` em `src/brew/brew.h` — **decodifique
qualquer endereço `0xF0000xxx` com `ZTRAP_ID()` antes de dizer "API faltando"**).
Estado atual (verificado): **10 dos 62 jogos completam o boot**; Double Dragon pinta a
tela e processa eventos; os 52 bloqueados caem em só 3 causas conhecidas. Fontes de
verdade: `RODADA_5_CONSOLIDADO.md` (leia o resumo de 10 linhas), `sweep_result.csv`
(classificação dos 62 jogos), `STATUS_R5_*.md`, `docs/PROGRESS.md`.

### Compilar e testar (copie e cole)

```powershell
cd <SEU WORKTREE>
msbuild zeebo_libretro.sln /p:Configuration=Release /p:Platform=x64 /m:4 /v:minimal
tests\libretro_smoke.exe x64\Release\zeebo_libretro.dll tests\roms\real_ddragon_game.mod
```

Na saída (stderr): `[Zeebo]` = progresso do boot; `frame N: ... instrucoes=X ...
fb[0]=0x...` a cada 60 frames (se `instrucoes` congela entre frames, a CPU estacionou);
`CPU descarrilou: fetch em 0x...` + trace de 64 PCs = travou. O smoke test aceita
simulação de input por env vars (ver `tests/libretro_smoke.c`, adicionado na rodada 5).

### ⚠️ Regras (iguais pra todo mundo, sem exceção)

1. **Worktree obrigatório** (nunca trabalhe no diretório principal):
   ```powershell
   cd C:\Users\Lucas\source\repos\zeebo_libretro
   git worktree add ..\zeebo_libretro-<slug> -b rodada6/<slug> master
   cd ..\zeebo_libretro-<slug>
   ```
2. Rode o smoke test ANTES de mudar qualquer coisa (baseline) e DEPOIS (comparação).
3. Nunca escreva "✅ funciona" sem colar o log de depois. **Evidência > afirmação.**
4. Antes de encerrar: smoke test nos 4 jogos Tier 1 comparando `instrucoes=`/`fb[0]=`
   com a baseline — mudou pra pior em jogo que "não era o seu", reporte (não esconda).
5. Commits pequenos em pt-BR com `Co-Authored-By: <seu nome> <noreply@anthropic.com>`.
   Nunca em `master`, nunca `push --force`, nunca commitar mudança de toolset no
   `.vcxproj` (v142/v143 é ajuste local da máquina).
6. **Se empacar por mais de ~30 min no mesmo ponto**: pare, escreva no seu STATUS
   exatamente onde parou (log + o que tentou) e encerre. Diagnóstico honesto vale
   mais que chute — outra rodada continua de onde você parou.
7. Logs `LOGE`/`LOGD` temporários de investigação: reverta antes do commit final.

---

## 🅰️ TAREFA A — O bug dos 13 jogos: descarrilamento em `0x12000000` (A MAIS VALIOSA)

**Branch:** `rodada6/regiontable` · **Saída:** `STATUS_R6_A_REGIONTABLE.md`
**Escopo:** `src/loader/mod_loader.c` (e leitura de qualquer coisa). Difícil — leia
`STATUS_C_BOOT_PACMANIA_ZEEBOIDS.md` (seção Zeeboids) e `RODADA_5_CONSOLIDADO.md`
(Achado nº 2) ANTES de tudo.

**O fato**: 13 jogos (`274214` cnk2/Crash, `278986` cninja, `278987` spinmast,
`278988` strhoop, `279036` game, `279125` supbtime, `279126` karnovr, `279173`
wizdfire, `279200` magdrop3, `279233` darkseal, `279888` baddudes, `279889` hbarrel)
descarrilam TODOS em `PC=0x12000000` (fim da heap), com o PC andando de 4 em 4 por
memória zerada. É o mesmo padrão que bloqueava o Zeeboids antes do HLE do bootstrap
PIC (`e747bd8`) — mas nesses 13 o HLE existente **não está bastando**.

**Passo a passo:**
1. Escolha 2 jogos do lote (sugestão: `279125` supbtime e `274214` cnk2). Rode o smoke
   em cada um e cole o log completo do loader (`mod1:` / `mod raw` / `bootstrap PIC
   pulado via HLE`). Pergunta-chave: **o log mostra "bootstrap PIC pulado via HLE"?**
   - Se NÃO mostra: o header desses `.mod` não bate com a condição `hle_scatter` em
     `zmod_load()` (`mod_loader.c` ~linha 176). Dumpe os primeiros 0x40 bytes do
     arquivo (Python: `open(...,'rb').read(0x40).hex()`) e compare os campos
     (`+0x10` hdr_size, `+0x18` code_off, `+0x1C` code_size, `+0x20` data_size,
     `+0x24` bss_size) com os do Pac-Mania (que funciona). Ajuste a condição ou o
     parse pra cobrir a variante nova — SEM quebrar os que já funcionam.
   - Se SIM mostra: o problema é depois do load. Veja o trace de 64 PCs: qual foi o
     último PC dentro do código real antes de "andar pela heap"? Desmonte ali
     (`pip install capstone`; endereço de arquivo = endereço virtual + code_off) e
     descubra que ponteiro levou a CPU pra heap (`0x10000000+`). Provável: um
     ponteiro de função lido de uma struct que deveria ter sido relocada.
2. Aplique o fix mínimo que fizer os 2 jogos-amostra passarem do ponto atual.
3. Valide no lote inteiro: rode o smoke nos 13 (use `sweep_68_games.ps1` filtrado ou
   um loop simples) e conte quantos saem de "descarrilou em 0x12000000".
4. Regressão nos 4 Tier 1 + Zeeboids (usa o mesmo caminho de loader!).

**Meta:** ≥5 dos 13 jogos mudando de categoria. Se não conseguir fix, entregue o
diagnóstico exato (campos de header dumpados + desmontagem do ponto de fuga).

---

## 🅱️🅲️🅳️ TAREFAS B, C e D — Catalogar CLSIDs (37 jogos, 3 lotes — MECÂNICA)

**Branches:** `rodada6/clsid-lote1`, `rodada6/clsid-lote2`, `rodada6/clsid-lote3`
**Saídas:** `STATUS_R6_B_CLSID1.md`, `STATUS_R6_C_CLSID2.md`, `STATUS_R6_D_CLSID3.md`
**Escopo:** `src/loader/mif_parser.c` (função `clsid_from_path_hint`) e
`src/brew/aee_ids.h` (tabela de nomes). NADA além desses 2 arquivos.

**O fato**: 37 jogos falham só porque o CLSID do applet não é resolvido
(`CreateInstance` recebe 0). A lista exata está em `sweep_result.csv` (categoria
"CreateInstance falhou"). Divisão dos lotes: ordene os 37 por id numérico —
**B = os 12 primeiros, C = os 12 seguintes, D = os 13 últimos.**

**Método por jogo (repita mecanicamente, ~10 min/jogo):**
1. Ache candidatos que aparecem **tanto no `.mif` quanto no `.mod`** — o CLSID do
   applet existe nos dois (o `.mif` declara, o `AEEClsCreateInstance` do `.mod`
   compara). Script Python pronto (adapte os caminhos):
   ```python
   import re, itertools
   mif = open(r"...\274804\mif\<ID>.mif","rb").read()
   mod = open(r"...\274804\mod\<ID>\<jogo>.mod","rb").read()
   # candidatos: 4 bytes little-endian, byte alto 0x01, segundo byte 0x01-0x0F
   def cands(b):
       s=set()
       for i in range(0,len(b)-3):
           v=int.from_bytes(b[i:i+4],"little")
           if 0x01010000 <= v <= 0x010FFFFF or 0x01000000 <= v <= 0x0100FFFF: s.add(v)
       return s
   inter = cands(mif) & cands(mod)
   # remove CLSIDs de sistema conhecidos (aparecem em todos os jogos):
   sistema = {0x0100101C, 0x01001001, 0x01001056}  # complete com os de aee_ids.h
   print([hex(v) for v in sorted(inter - sistema)])
   ```
   O CLSID do applet costuma ser o candidato "raro" (não aparece nos outros jogos).
   Se sobrar mais de um candidato, teste um por vez no passo 2.
2. Cadastre o candidato em `clsid_from_path_hint()` (`mif_parser.c`) com o nome do
   arquivo `.mod` do jogo (mesmo padrão da entrada "Zeeboids") e em `aee_ids.h`.
3. Compile e rode o smoke contra o `.mod` do jogo. Confirmação = o log mostra
   `CreateInstance retornou ...` com `applet=0x10......` (diferente de 0) e de
   preferência `jogo RODANDO`. Se o applet continuar 0, o candidato era errado —
   volte ao passo 1 e teste o próximo.
4. Anote no seu STATUS: id, arquivo, CLSID confirmado (ou "candidatos testados e
   falhos: ..."), e até onde o jogo chega agora (rodando? descarrila onde?).

**Meta por lote:** ≥6 dos 12-13 jogos com CLSID confirmado e cadastrado. Commit por
jogo confirmado (mensagem: `CLSID do <jogo> confirmado: 0x...`).

---

## 🅴️ TAREFA E — Fazer o controle FUNCIONAR num jogo de verdade

**Branch:** `rodada6/input-play` · **Saída:** `STATUS_R6_E_INPUT_PLAY.md`
**Escopo:** `src/brew/boot.c` (só a parte de input/HID/eventos em `zboot_tick` e os
cases do IHID), `src/input/input.c`, `tests/libretro_smoke.c`.

**Contexto direto do Lucas**: testando no RetroArch, jogos já mostram texto e um até
abriu — **mas o controle não funcionava**. A rodada 5 (`STATUS_R5_E_INPUT.md` — LEIA)
deixou pronto: signal de botão do HID agora dispara callback real (Double Dragon
executa +73 instruções ao apertar botão), e o smoke test simula input por env vars.
Mas "callback executa" ainda não é "o jogo reage visivelmente".

**Passo a passo:**
1. Baseline: Double Dragon com input simulado (env vars do smoke — veja no início de
   `tests/libretro_smoke.c` os nomes exatos). O `fb[0]`/instruções mudam quando o
   botão é apertado vs. sem apertar? Teste botões diferentes (o id do botão START/A/
   direcional — mapa em `src/input/input.c`).
2. O caminho signal→callback entrega só "acorda"; o jogo então chama
   `IHIDDevice_GetNextEvent` (case 9) pra ler o evento. Verifique no log se o
   `GetNextEvent` é chamado após o callback e se devolve os campos certos
   (`id`/`uid`/`down`). Se o jogo chama e recebe lixo/vazio, o bug está no case 9 —
   confira o layout do que ele escreve nos out-params.
3. Jogos que NÃO usam HID (Pac-Mania, e provavelmente os que o Lucas viu abrir):
   input em BREW clássico chega por **evento `EVT_KEY` no `HandleEvent` do applet**.
   O `HandleEvent` já é chamado no boot via `guest_call` — reutilize: em
   `zboot_tick()`, quando um botão muda e NÃO há signal HID registrado, chame
   `guest_call(handle_evt, applet, EVT_KEY, keycode, 0)`. **Não chute os códigos**:
   descubra o valor de `EVT_KEY`/`AVK_*` que este SDK usa olhando a referência do
   zeemu (`reference/zeemu-main/`, procure `EVT_KEY` e `AVK_`) — lembre que este SDK
   já provou usar códigos fora do padrão (EVT_APP_START = 0 aqui).
4. Valide com o jogo que reagir melhor. Evidência = mudança de `fb[0]`/pixels/log
   claramente causada pelo botão (rode com e sem input e cole a diferença).

**Meta:** UM jogo comprovadamente reagindo a botão no smoke test. Bônus: instruções
pro Lucas testar no RetroArch (que botão apertar em qual jogo).

---

## 🅵️ TAREFA F — Family Pack: o callback que lê `0xEA000014` (DIFÍCIL)

**Branch:** `rodada6/fp-callback` · **Saída:** `STATUS_R6_F_FP_CALLBACK.md`
**Escopo:** `src/brew/boot.c` (cases do IHID/signals do Family Pack). Leia antes:
`RODADA_5_CONSOLIDADO.md` (seção "Detalhe extra") e `STATUS_R5_A_SIGNALS.md`.

**O fato**: com signals funcionando, o `DeviceConnectCB` do Family Pack roda de
verdade — mas tenta ler `0xEA000014` e `0xEA000024` e entra em loop (contido pelo
watchdog). Pista forte: `0xEA......` é o padrão de uma **instrução ARM de branch**
(`B`) — ou seja, o jogo leu **bytes de código como se fossem um ponteiro**. Algum
campo de struct que o core devolve pro jogo (provavelmente no `GetConnectedDevices`/
`GetDeviceInfo`/`CreateDevice` do IHID, ou no out-param do signal) contém código em
vez de ponteiro, ou o `user`/`this` passado ao callback está errado.

**Passo a passo:**
1. Reproduza: smoke do Family Pack, log do momento em que `DeviceConnectCB` roda —
   colete R0-R3 na entrada do callback (LOGD temporário no `guest_call` do dispatch
   de signal).
2. De onde veio `0xEA000014`? Adicione um watchpoint barato: em `zmem_read32`, logue
   quando o retorno for `0xEA000014` (com o endereço lido e o PC). O endereço lido
   diz qual struct/campo está errado.
3. Compare o layout da struct que o core preenche (cases do IHID em `boot.c`) com o
   que o jogo espera (desmonte o `DeviceConnectCB` em volta do PC do passo 2 — os
   offsets `+0x14`/`+0x24` que ele soma ao ponteiro-base dizem o layout esperado).
4. Corrija o campo/layout. Meta: o callback completa sem watchdog e o jogo avança —
   ideal: primeiras chamadas GL voltando (`grep -c glClear` > 0, hoje é 0).
5. Regressão nos 4 Tier 1 (o IHID é compartilhado — Double Dragon usa!).

---

## 🅶️ TAREFA G — Pac-Mania: janela longa e o que ele espera pra desenhar

**Branch:** `rodada6/pacmania-longo` · **Saída:** `STATUS_R6_G_PACMANIA.md`
**Escopo:** `tests/libretro_smoke.c` (nº de frames configurável), leitura livre,
fixes só em `src/brew/helpers.c`/`src/brew/isound.c` se o bloqueio cair aí.

**O fato** (`STATUS_R5_C_PACMANIA_DRAW.md` — LEIA): o `DrawText` funciona (testado
isoladamente), mas o Pac-Mania fica em loop de timers de 1ms/10ms sem nunca chamar
desenho nos 180 frames do smoke. Ele está esperando alguma coisa completar.

**Passo a passo:**
1. Torne o número de frames do smoke configurável por env var (hoje fixo em 180) —
   commit separado, vira infraestrutura.
2. Rode o Pac-Mania por 1800 frames (30s simulados). Ele desenha em algum momento?
   Se sim, quanto tempo leva e o que aparece (`fb[0]` + dump de alguns pixels)?
3. Se continua preto: liste (com um LOGD temporário de contagem) TODAS as traps/stubs
   que ele chama no loop — o que ele consulta repetidamente? Candidatos clássicos:
   `ISOUND`/`IMEDIA` esperando "pronto" que nunca vem (nosso ISound é stub —
   devolvemos o status certo?), `IFILE` lendo resource, `GetTimeMS`/`GetUpTimeMS`
   esperando avançar (avançamos 16ms/frame — confira), callback de `ISHELL_Resume`
   nunca disparado.
4. Fix mínimo no que for encontrado (ex.: stub de som devolvendo "playback completo"
   em vez de pendente-eterno).

**Meta:** Pac-Mania desenhando qualquer coisa, OU o nome exato da API em que ele
está preso, com evidência de contagem de chamadas.

---

## 🅷️ TAREFA H — Triagem dos 6 novos jogos que já bootam (MECÂNICA)

**Branch:** `rodada6/triagem-tier2` · **Saída:** `STATUS_R6_H_TRIAGEM.md`
**Escopo:** só diagnóstico + logs temporários (revertidos). Nenhum fix em `src/`.

Jogos: **Quake** (`274802`), **Quake II** (`276153`), **Brain Challenge** (`274804`,
`brainchallenge.mod`), **Reksio** (`277495`), **Bajaz** (`277727`), **Zeebo App**
(`274791`). Todos completam o boot (varredura da rodada 5) mas ninguém olhou o que
fazem depois.

**Para CADA jogo, preencha a mesma ficha (mecânico):**
1. Smoke test com log completo salvo em arquivo.
2. Instruções nos frames 1/61/121 (cresce? congela? quanto?).
3. `fb[0]` muda? Alguma chamada de desenho (IDisplay/GL) no log?
4. Que CLSIDs ele pede no `CreateInstance` depois do boot? Quais caem em stub?
5. Registra HID/signals/timers? Quais?
6. Veredito de 1 linha: "bloqueado em X, parece precisar de Y".

**Meta:** 6 fichas completas + ranking de qual jogo está mais perto de mostrar
imagem (vira alvo dedicado na Rodada 7). Brain Challenge roda 5,9M instruções — é
o candidato mais quente.

---

## 🅸️ TAREFA I — Zeeboids: por que fica parado depois do boot

**Branch:** `rodada6/zeeboids-idle` · **Saída:** `STATUS_R6_I_ZEEBOIDS.md`
**Escopo:** diagnóstico livre; fix só se for pequeno e no território de
timers/signals/helpers (não mexa no IHID da Tarefa F).

**O fato**: Zeeboids boota (544.678 instruções) e congela — não registra timer, não
registra HID, não usa os signals novos. Ele espera ALGUMA coisa.

1. LOGD temporário: liste as últimas 20 traps/stubs que ele chama antes de parar, e
   o estado (`halted`, PC, o que o último guest_call retornou).
2. Hipóteses pra checar em ordem: (a) registrou um callback via
   `ISHELL_Resume`/`AEECallback` que nunca disparamos; (b) espera um evento
   `EVT_APP_RESUME`/`EVT_KEY` depois do start; (c) pediu um CLSID que devolvemos
   como stub inerte e ficou esperando resposta dele; (d) o retorno do
   `EVT_APP_START` dele (`ret=0x...`) indica que ele quer ser chamado de novo.
3. Teste barato: mande um `HandleEvent` extra (ex.: o mesmo `EVT_APP_START=0` de
   novo, ou valores 1..5) via guest_call depois de 60 frames e veja se ele acorda
   (instruções voltam a crescer). Documente qual valor teve efeito.

**Meta:** nome exato do mecanismo que falta pro Zeeboids, com evidência — fix se
couber no escopo.

---

## 🅹️ TAREFA J — Validação, integração e consolidação

**Branch:** `rodada6/validacao` · **Saída:** `RODADA_6_CONSOLIDADO.md`
**Escopo:** leitura de tudo; escrita só de docs/scripts/merges. NÃO mexe em `src/`.

1. **Fase 1 (já)**: re-rode `sweep_68_games.ps1` na baseline `master` atual e guarde
   o CSV (a rodada 5 rodou a varredura numa DLL pré-integração — refaça na atual pra
   ter baseline limpa desta rodada).
2. **Fase 2 (quando A-I terminarem)**: worktree de integração
   (`rodada6/integracao`), merge na ordem **A → B → C → D → E → F → G → I** (H e J
   não têm código). Conflitos prováveis: B/C/D tocam os MESMOS 2 arquivos
   (`mif_parser.c`/`aee_ids.h`) — o conflito é trivial (listas de entradas), resolva
   mantendo todas as entradas dos 3 lotes.
3. Rebuild + smoke nos 4 Tier 1 + **re-varredura completa dos 62** na DLL integrada.
   O número da rodada é: **quantos jogos saíram de cada categoria** (rodando /
   CreateInstance falhou / 0x12000000 / outro) vs. a baseline da Fase 1.
4. `RODADA_6_CONSOLIDADO.md`: resumo de 10 linhas em linguagem simples pro Lucas,
   tabela antes→depois das categorias, tabela por tarefa (entregue/não + evidência),
   próximo passo nº 1 da Rodada 7.
5. Atualize o vault Obsidian (`G:\...\12_Estado_Atual\00_STATUS_ATUAL.md` e
   `02_HISTORICO_PROGRESSO.md`) se tiver acesso; senão, seção "para colar no
   Obsidian" no consolidado.

---

## 🔄 SINCRONIZAÇÃO

1. Resumo de ≤8 linhas no topo de cada `STATUS_R6_*.md`: o que mudou, se compila, log
   de teste real colado, resultado (não intenção).
2. Handoffs via arquivos de status + cherry-pick entre branches `rodada6/*` — nunca
   editar worktree alheio.
3. Só a Tarefa J faz merge, e em `rodada6/integracao` (não em `master`) — o Lucas
   aprova o merge final depois de testar no RetroArch.
4. Ao terminar: `git worktree remove ..\zeebo_libretro-<slug>` (a branch fica).
