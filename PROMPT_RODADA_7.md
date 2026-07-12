# 🎯 PROMPT — Rodada 7: 10 IAs em Paralelo (zeebo_libretro)

> Cole este arquivo inteiro na conversa de cada IA. A atribuição vem na hora do envio:
> o Lucas escreve **"você é responsável pela TAREFA D"** junto com o prompt. Faça SÓ a
> sua tarefa, dentro do escopo dela. Sem tarefa indicada? Pergunte antes. Tudo em pt-BR.
>
> **Nota pro Lucas ao distribuir**: **A** e **F** são as difíceis (engenharia reversa) —
> se tiver modelo mais forte, mande nelas. B, C, D e H são mecânicas, ideais pra Haiku.

---

## 🗺️ LOCAIS NO PC

| Local | Caminho | O que é |
|---|---|---|
| **Repositório** | `C:\Users\Lucas\source\repos\zeebo_libretro` | Branch `master` já contém TODO o trabalho das rodadas 1-6 (incluindo os 4 fixes da sessão interrompida da rodada 6: bootstrap ARMCC cru, SXTB/SXTH/UXTB/UXTH, IDisplay1, adaptador QEGL). |
| **ROMs Tier 1** | `...\zeebo_libretro\tests\roms\` | `real_pacmania_game.mod`, `real_family_pack_game.mod`, `real_ddragon_game.mod`, `real_zeeboids_game.mod`. |
| **Romset (62 jogos)** | `C:\Users\Lucas\Downloads\zeebo-romset-and-devtools\Zeebo\Zeebo\Zeebo Game & App Compilation - OpenZeebo\274804\` | `mif\<id>.mif` e `mod\<id>\<jogo>.mod`. |
| **RetroArch** | `C:\Program Files (x86)\Steam\steamapps\common\RetroArch\cores` | Onde a DLL é instalada. Logs em `...\RetroArch\logs`. |
| **Vault Obsidian** | `G:\Meu Drive\Documentos obi\Projeto\Emulador` | Só leitura (Tarefa J atualiza no fim). |

## 🧭 ONBOARDING EM 1 PARÁGRAFO

Core libretro que emula o **Zeebo** (2009): jogos são applets **BREW** em ARM; o core
interpreta a CPU e reimplementa o BREW via HLE (traps `0xF0000000+`; decodifique
qualquer `0xF0000xxx` com `ZTRAP_ID()` de `src/brew/brew.h` antes de dizer "API
faltando"). Fontes de verdade: `RODADA_5_CONSOLIDADO.md`, `sweep_result.csv`,
`STATUS_R5_*.md`, `docs/PROGRESS.md`.

### O que mudou desde a varredura da rodada 5 (fixes da rodada 6, JÁ em master)

1. **O bug dos 13 jogos (`0x12000000`) foi RESOLVIDO** (`d3699a4`): o próprio loader
   corrompia a palavra em `+0x10` do bootstrap ARMCC cru (sobrescrevia o literal com a
   tabela de helpers → a CPU executava a tabela como código e atravessava o heap).
2. Instruções ARMv6 SXTB/SXTH/UXTB/UXTH implementadas (`591f59c`).
3. `IDisplay1` (`0x010127D4`) roteado pro backend real de display (`269de1c`) — slot 16
   `GetDeviceBitmap` funciona.
4. Adaptador **QEGL** criado (`4c97f67`): 2 `QueryInterface` (`0x0103D8DD`/`0x0103D8EA`),
   `InitGLSurface` (slot 4) e criação de superfície com out-pointer (slot 5).

**Resultado verificado por smoke test**: 11 dos 12 jogos do antigo lote `0x12000000`
agora chegam a "RODANDO", e 10 deles (ports Data East: Super BurgerTime, Bad Dudes,
Karnov's Revenge etc.) **desenham na tela, eles mesmos, a mensagem `InitGLSurface
failed`** via `IDisplay_DrawText` — ou seja, texto real renderizado pelo jogo, mas a
inicialização gráfica QEGL ainda é rejeitada por eles. O Crash (`274214`) saiu do
crash e agora só falta CLSID. **Nenhuma regressão nos Tier 1** (Double Dragon
2.756.802 instruções / fb branco; Pac-Mania 145.686; Zeeboids 544.678 — idênticos).

### Compilar e testar (copie e cole)

```powershell
cd <SEU WORKTREE>
msbuild zeebo_libretro.sln /p:Configuration=Release /p:Platform=x64 /m:4 /v:minimal
tests\libretro_smoke.exe x64\Release\zeebo_libretro.dll tests\roms\real_ddragon_game.mod
```

Saída (stderr): `[Zeebo]` = boot; `frame N: ... instrucoes=X ... fb[0]=0x...` a cada 60
frames (`instrucoes` congelado = CPU estacionada); `CPU descarrilou` + trace de 64 PCs =
travou. O smoke aceita simulação de input e nº de frames via env vars (veja o topo de
`tests/libretro_smoke.c`).

### ⚠️ Regras (todas as tarefas)

1. **Worktree obrigatório**:
   ```powershell
   cd C:\Users\Lucas\source\repos\zeebo_libretro
   git worktree add ..\zeebo_libretro-<slug> -b rodada7/<slug> master
   cd ..\zeebo_libretro-<slug>
   ```
2. Smoke test ANTES de mudar (baseline) e DEPOIS (comparação).
3. **Evidência > afirmação** — nunca "✅ funciona" sem log colado.
4. Antes de encerrar: smoke nos 4 Tier 1 comparando `instrucoes=`/`fb[0]=` com a
   baseline. Piorou algo fora do seu alvo? Reporte, não esconda.
5. Commits pequenos em pt-BR com `Co-Authored-By: <seu nome> <noreply@anthropic.com>`.
   Nunca em `master`, nunca `push --force`, nunca commitar toolset do `.vcxproj`.
6. **Empacou ~30 min no mesmo ponto?** Documente exatamente onde (log + tentativas) no
   seu STATUS e encerre. Diagnóstico honesto > chute.
7. `LOGE`/`LOGD` temporários: reverta antes do commit final.

---

## 🅰️ TAREFA A — QEGL: fazer o `InitGLSurface` SUCEDER (10 jogos de uma vez — DIFÍCIL)

**Branch:** `rodada7/qegl-surface` · **Saída:** `STATUS_R7_A_QEGL.md`
**Escopo:** `src/brew/boot.c` (cases do QEGL), `src/gpu/egl_gl.c/h`.

**O fato**: 10 ports Data East chamam `IQEGL_InitGLSurface`, o core devolve um objeto
(log: `IQEGL_InitGLSurface: bitmap=0x1002D698 out=0x2FFFFF30 -> 0x1002D828`) — e mesmo
assim o jogo desenha `InitGLSurface failed`. Alguma checagem pós-chamada falha.

**Passo a passo (jogo-amostra: `279125` supbtime; segundo: `279888` baddudes):**
1. Baseline com log completo salvo.
2. Ache a decisão de falha no binário: procure a string `InitGLSurface failed` no
   `.mod` (Python `data.find(b"InitGLSurface")`), ache a referência a esse endereço no
   código (busca pelos bytes do endereço ou por instruções `ADR`/`LDR` próximas) e
   desmonte (capstone) a função que decide entre "seguir" e "desenhar erro". Os
   offsets/registradores testados ali dizem EXATAMENTE o que o jogo verifica: valor de
   retorno em R0? campo dentro do objeto retornado? uma das 2 interfaces de
   `QueryInterface` (`0x0103D8DD`/`0x0103D8EA`)? uma chamada subsequente a um slot da
   superfície que devolve erro?
3. Instrumente (LOGD temporário) as chamadas QEGL seguintes ao `InitGLSurface` no
   smoke — o jogo chama mais algum slot antes de desistir? Com quais args/retorno?
4. Corrija o adaptador pra satisfazer a checagem real (sem gambiarras de "retorna
   sempre 0" sem entender — o `STATUS` deve explicar o layout descoberto).
5. Meta: supbtime passa da tela de erro. Ideal: chamadas GL de verdade aparecendo
   (`glClear`/`glViewport`/...) — aí o rasterizador existente assume. Valide no lote
   (os 10) + regressão Tier 1 (Family Pack usa o EGL comum — não pode quebrar).

---

## 🅱️🅲️🅳️ TAREFAS B, C, D — Catalogar CLSIDs (38 jogos, 3 lotes — MECÂNICA)

**Branches:** `rodada7/clsid-lote1|2|3` · **Saídas:** `STATUS_R7_B|C|D_CLSID.md`
**Escopo:** SÓ `src/loader/mif_parser.c` (`clsid_from_path_hint`) e `src/brew/aee_ids.h`.

**O fato**: 37 jogos da varredura (categoria "CreateInstance falhou" no
`sweep_result.csv`) + o Crash (`274214`, destravado agora) falham só porque o CLSID do
applet não é resolvido. Ordene os 38 por id: **B = 13 primeiros, C = 13 do meio,
D = 12 últimos.**

**Método por jogo (~10 min, mecânico):**
1. Candidatos que aparecem **no `.mif` E no `.mod`** (o CLSID do applet existe nos dois):
   ```python
   def cands(b):
       s=set()
       for i in range(0,len(b)-3):
           v=int.from_bytes(b[i:i+4],"little")
           if 0x01000000 <= v <= 0x010FFFFF: s.add(v)
       return s
   mif=open(r"...\mif\<ID>.mif","rb").read(); mod=open(r"...\mod\<ID>\<jogo>.mod","rb").read()
   inter = cands(mif) & cands(mod)
   # subtraia CLSIDs de sistema (todos os que já estão em src/brew/aee_ids.h)
   print(sorted(hex(v) for v in inter))
   ```
   Dica de desempate: o CLSID do applet costuma aparecer POUCAS vezes no `.mod` (1-3)
   e perto do fim do `.mif`; valores estruturais aparecem em TODOS os jogos.
2. Cadastre o candidato em `clsid_from_path_hint()` (mesmo padrão da entrada
   "Zeeboids") + nome em `aee_ids.h`.
3. Compile e rode o smoke no jogo. Confirmação = `applet=0x10......` (≠0) no log,
   ideal `jogo RODANDO`. Falhou? Próximo candidato.
4. Commit por jogo confirmado (`CLSID do <jogo> confirmado: 0x...`). No STATUS: id,
   CLSID, até onde o jogo chega agora.

**Meta por lote:** ≥6 confirmados. Cada CLSID confirmado é +1 jogo dando boot.

---

## 🅴️ TAREFA E — Fazer o controle FUNCIONAR num jogo de verdade

**Branch:** `rodada7/input-play` · **Saída:** `STATUS_R7_E_INPUT.md`
**Escopo:** `src/brew/boot.c` (input/HID em `zboot_tick` + cases IHID),
`src/input/input.c`, `tests/libretro_smoke.c`. LEIA `STATUS_R5_E_INPUT.md` antes.

**Contexto do Lucas (teste real no RetroArch)**: um jogo abriu, **mas o controle não
funcionava**. A rodada 5 fez o signal de botão HID disparar callback real (Double
Dragon executa +73 instruções no aperto) — falta o jogo REAGIR visivelmente.

1. Baseline: Double Dragon com input simulado (env vars do smoke). `fb[0]`/instruções
   mudam com botão vs. sem? Teste vários botões (mapa em `src/input/input.c`).
2. Depois do callback ("acorda"), o jogo lê o evento via `IHIDDevice_GetNextEvent`
   (case 9). Confira no log se ele é chamado e se os out-params têm layout certo —
   se o jogo lê lixo, o bug é aí (desmonte o leitor pra confirmar offsets).
3. Jogos sem HID (Pac-Mania, Data East ports): input BREW clássico = **`EVT_KEY` no
   `HandleEvent` do applet**. Implemente em `zboot_tick()`: sem signal HID registrado
   e botão mudou → `guest_call(handle_evt, applet, EVT_KEY, keycode, 0)`. **Não chute
   códigos**: ache `EVT_KEY`/`AVK_*` na referência zeemu (`reference/zeemu-main/`) e
   lembre que este SDK já usou códigos fora do padrão (EVT_APP_START=0) — se
   necessário, desmonte o `HandleEvent` do jogo pra ver quais valores de evento ele
   compara.
4. Evidência final: rode com e sem input e cole a diferença (fb/instruções/log).

**Meta:** UM jogo comprovadamente reagindo a botão + instruções pro Lucas reproduzir
no RetroArch (qual jogo, qual botão).

---

## 🅵️ TAREFA F — Family Pack: o callback que lê `0xEA000014` (DIFÍCIL)

**Branch:** `rodada7/fp-callback` · **Saída:** `STATUS_R7_F_FP.md`
**Escopo:** `src/brew/boot.c` (IHID/signals do Family Pack). LEIA
`RODADA_5_CONSOLIDADO.md` (seção "Detalhe extra") e `STATUS_R5_A_SIGNALS.md`.

**O fato**: o `DeviceConnectCB` do Family Pack roda de verdade, mas lê `0xEA000014`/
`0xEA000024` como ponteiro e entra em loop (contido por watchdog). `0xEA......` é
instrução ARM de branch — o jogo leu **código como ponteiro**: algum campo devolvido
pelo core (structs do `GetConnectedDevices`/`GetDeviceInfo`/`CreateDevice`, ou o
`user` do callback) está errado.

1. Reproduza e capture R0-R3 na entrada do callback (LOGD no dispatch de signal).
2. Watchpoint barato: em `zmem_read32`, logue quando o VALOR retornado for
   `0xEA000014` (endereço lido + PC) — isso aponta a struct/campo exato.
3. Desmonte o callback em volta desse PC: os offsets que ele soma ao ponteiro-base
   revelam o layout esperado. Compare com o que o core preenche e corrija.
4. Meta: callback completa sem watchdog e o Family Pack avança — ideal: primeiras
   chamadas GL voltando (`grep -c glClear` > 0; hoje é 0). Regressão: Double Dragon
   usa o mesmo IHID.

---

## 🅶️ TAREFA G — Pac-Mania: janela longa e o que ele espera pra desenhar

**Branch:** `rodada7/pacmania-longo` · **Saída:** `STATUS_R7_G_PACMANIA.md`
**Escopo:** `tests/libretro_smoke.c`, leitura livre; fixes só em `src/brew/helpers.c`/
`src/brew/isound.c`. LEIA `STATUS_R5_C_PACMANIA_DRAW.md`.

1. Rode o Pac-Mania por 1800+ frames (nº de frames já é configurável por env var; se
   não for, torne — commit separado). Desenha em algum momento? Quanto tempo?
2. Se continua preto: conte (LOGD temporário) TODAS as traps/stubs chamadas no loop de
   timers 1ms/10ms. O que ele consulta repetidamente? Candidatos: `ISOUND`/`IMEDIA`
   esperando "pronto" eterno, `IFILE` de resource, `GetTimeMS` (avançamos 16ms/frame —
   confira), callback de `ISHELL_Resume` nunca disparado.
3. Fix mínimo no que achar (ex.: stub de som devolvendo "playback completo").

**Meta:** Pac-Mania desenhando qualquer coisa, OU o nome exato da API em que está
preso, com contagem de chamadas como evidência.

---

## 🅷️ TAREFA H — Triagem dos 6 jogos que bootam há 2 rodadas (MECÂNICA)

**Branch:** `rodada7/triagem` · **Saída:** `STATUS_R7_H_TRIAGEM.md`
**Escopo:** só diagnóstico + logs temporários (revertidos). Nenhum fix em `src/`.

Jogos: **Quake** (`274802`), **Quake II** (`276153`), **Brain Challenge** (`274804`,
`brainchallenge.mod` — 5,9M instruções, o mais quente), **Reksio** (`277495`),
**Bajaz** (`277727`), **Zeebo App** (`274791`).

**Ficha por jogo (mecânico):** 1) smoke com log salvo; 2) instruções nos frames
1/61/121; 3) `fb[0]` muda? chamadas de desenho no log?; 4) CLSIDs pedidos pós-boot e
quais caem em stub; 5) registra timers/signals/HID?; 6) veredito de 1 linha
("bloqueado em X, precisa de Y").

**Meta:** 6 fichas + ranking de qual está mais perto de mostrar imagem (vira alvo da
Rodada 8).

---

## 🅸️ TAREFA I — Zeeboids: por que fica parado depois do boot

**Branch:** `rodada7/zeeboids` · **Saída:** `STATUS_R7_I_ZEEBOIDS.md`
**Escopo:** diagnóstico livre; fix só se pequeno e em timers/signals/helpers (IHID é
da Tarefa F).

1. LOGD: últimas 20 traps/stubs antes de parar (544.678 instruções e congela — não
   registra timer nem HID).
2. Cheque em ordem: (a) `ISHELL_Resume`/`AEECallback` registrado e nunca disparado;
   (b) espera `EVT_APP_RESUME`/`EVT_KEY`; (c) CLSID stub inerte de que espera
   resposta; (d) o `ret=0x...` do `EVT_APP_START` pede novo evento.
3. Teste barato: `guest_call(handle_evt, applet, N, 0, 0)` com N = 1..5 depois de 60
   frames — algum acorda o jogo (instruções voltam a crescer)? Documente qual.

**Meta:** nome exato do mecanismo que falta, com evidência — fix se couber.

---

## 🅹️ TAREFA J — Validação, integração e consolidação

**Branch:** `rodada7/validacao` · **Saída:** `RODADA_7_CONSOLIDADO.md`
**Escopo:** leitura de tudo; escrita só de docs/scripts/merges. NÃO mexe em `src/`.

1. **Fase 1 (já)**: re-rode `sweep_68_games.ps1` no `master` atual → baseline desta
   rodada (a última varredura foi ANTES dos fixes da rodada 6; os números de categoria
   mudaram: o lote `0x12000000` virou "rodando com InitGLSurface failed").
2. Limpeza: remova worktrees órfãos de rodadas antigas que ainda existirem
   (`git worktree list`; os de `rodada3/*` têm restos não commitados — confira se o
   diff é lixo antes de remover com `--force`, e registre o que descartou).
3. **Fase 2 (quando A-I terminarem)**: worktree `rodada7/integracao`, merge na ordem
   **A → B → C → D → E → F → G → I** (B/C/D conflitam entre si nos mesmos 2 arquivos —
   conflito trivial de listas, mantenha todas as entradas).
4. Rebuild + smoke nos 4 Tier 1 + re-varredura completa. O número da rodada:
   **quantos jogos mudaram de categoria** vs. a Fase 1.
5. `RODADA_7_CONSOLIDADO.md`: resumo de 10 linhas em linguagem simples pro Lucas,
   tabela antes→depois, tabela por tarefa com evidência, próximo passo nº 1 da
   Rodada 8. Atualize o vault Obsidian (`G:\...\12_Estado_Atual\`) se tiver acesso.

---

## 🔄 SINCRONIZAÇÃO

1. Resumo de ≤8 linhas no topo de cada `STATUS_R7_*.md` (o que mudou, compila?, log
   real colado, resultado).
2. Handoffs via STATUS + cherry-pick entre branches `rodada7/*` — nunca editar
   worktree alheio.
3. Só a Tarefa J faz merge, e em `rodada7/integracao` — o Lucas aprova o merge final
   depois de testar no RetroArch.
4. Ao terminar: `git worktree remove ..\zeebo_libretro-<slug>`.
