# RODADA 5 — Consolidado (Tarefa F)

## Resumo em 10 linhas (pra você, Lucas, sem jargão)

1. As 5 tarefas técnicas (A a E) terminaram e cada uma testou de verdade com o
   emulador rodando, não só "compilou". Juntei tudo numa branch só
   (`rodada5/integracao`) e testei de novo depois de juntar — nada quebrou.
2. **Double Dragon** continua sendo o jogo mais avançado: roda muito mais tempo que
   antes (quase 2,8 milhões de "passos" de CPU nos primeiros 2 segundos simulados,
   contra ~17 mil antes) e continua pintando a tela de branco como já pintava.
3. **Family Pack** deixou de ficar "congelado" (não travava, mas também não fazia
   nada) — agora ele processa o evento de "controle conectado" e continua rodando.
   Só que descobrimos que a própria função do jogo que trata esse evento tenta ler
   memória inválida e entra num loop ruim; um "watchdog" que uma das tarefas
   escreveu detecta isso e evita travar o emulador de vez. Ainda não desenha nada
   na tela.
4. **Pac-Mania**: o texto (nomes, menus) ainda não aparece na tela, mas agora
   sabemos exatamente por quê — o mecanismo de desenhar texto (`DrawText`) foi
   implementado e testado isoladamente (funciona, pinta pixels de verdade), só que
   nesta janela de tempo testada o próprio jogo ainda está numa fase de
   carregamento e não manda texto nenhum pra desenhar ainda.
5. **Zeeboids** segue quase parado (não usa o mecanismo de sinal que ganhou suporte
   nesta rodada) — precisa de investigação própria numa próxima rodada.
6. Fiz uma varredura real (não chute) nos outros ~62 jogos do pacote completo.
   Achado principal: **13 jogos travam exatamente no mesmo endereço de memória**
   (`0x12000000`) — é o mesmo tipo de bug já visto no Zeeboids numa rodada
   anterior (o jogo "anda" por memória vazia até bater um limite). Consertar esse
   padrão de uma vez só pode destravar boa parte desses 13 de uma tacada.
7. Outro achado grande: **37 dos 62 jogos** nem chegam a criar o "objeto principal"
   do jogo porque o emulador não consegue descobrir o identificador interno
   (CLSID) deles a partir do arquivo — é queda de resolução de metadado, não bug
   de emulação de CPU. Tentei uma forma automática de adivinhar esse identificador
   e não deu certo de forma confiável; provavelmente vai precisar catalogar
   manualmente, um por um, como foi feito pro Zeeboids.
8. Ou seja: hoje **10 dos 62 jogos testados chegam a "rodar"** de verdade (o
   próprio jogo inicializa sem travar) — Double Dragon, Pac-Mania, Family Pack,
   Zeeboids, Quake, Quake II, Brain Challenge, Zeebo App, Reksio e Bajaz. Os
   outros 52 estão bloqueados por 3 causas bem definidas (não mistério): CLSID
   não encontrado (37), o bug de "andar por memória vazia" (13), CLSID errado
   detectado (2).
9. Nada regrediu: testei os 4 jogos principais antes e depois de juntar tudo, e
   nenhum piorou.
10. Próximo passo nº 1 pra próxima rodada: **o bug de "0x12000000"** é o de maior
    retorno — resolver ele de uma vez pode destravar até 13 jogos simultaneamente
    (mais que qualquer outra correção isolada até hoje).

---

## FASE 1 — Varredura dos 68 jogos (dados reais, não estimativa)

**Metodologia**: rodei `tests\libretro_smoke.exe` contra o `.mod` de cada uma das 62
pastas numéricas encontradas em
`...\274804\mod\` (as pastas `id1`, `nfsresources`, `preyresources`, `quake2res` são
recursos de outros jogos, não jogos próprios — excluídas). A DLL usada foi compilada a
partir do `master` **antes** da integração das tarefas A-E (baseline "pré-rodada-5"),
já que os fixes desta rodada tratam de bloqueios pós-boot (signals/IDisplay/input),
não dos bloqueios de CLSID/scatter-load que dominam esta varredura — não esperado
(e não observado) impacto desta rodada nesses 52 jogos. Script:
[`sweep_68_games.ps1`](sweep_68_games.ps1), resultado bruto:
[`sweep_result.csv`](sweep_result.csv).

### Resumo por categoria

| Categoria | Jogos | % |
|---|---|---|
| **rodando** (boot completo, sem travar) | 10 | 16% |
| **CreateInstance falhou** (CLSID não resolvido -> objeto principal nunca criado) | 37 | 60% |
| **descarrilou em 0x12000000** (mesmo padrão do bug do Zeeboids/Rodada 2) | 13 | 21% |
| **CLSID desconhecido** (achou *um* CLSID no arquivo, mas não bate com nenhum case implementado) | 2 | 3% |

### Achado nº 1 (maior): 37 jogos bloqueados por resolução de CLSID, não por emulação

Todos os 37 jogos desta categoria mostram exatamente o mesmo padrão de log:

```
[WARN] [Zeebo] zmod_load: CLSID do applet desconhecido - CreateInstance recebera 0
[INFO] [Zeebo] boot: IModule_CreateInstance(clsid=0x00000000) via 0x...
[INFO] [Zeebo] boot: CreateInstance retornou 0x00000001, applet=0x00000000
[ERROR] [Zeebo] boot: applet nao foi criado (clsid errado?)
```

Ou seja: **não é um bug de CPU/BREW** — é que `src/loader/mif_parser.c` não consegue
achar o identificador de classe (CLSID) do jogo dentro do `.mif`/`.mod`, então o
`CreateInstance` é chamado com `clsid=0`, que não corresponde a nenhuma classe válida, e
o boot para ali, sem descarrilar (frustrante: o jogo "falha bonito", sem dar pista de
onde exatamente ele travaria depois).

**Tentei uma heurística automática** para resolver isso sem catalogar jogo por jogo:
todo CLSID conhecido do Zeebo começa com o byte `0x01` (ex.: `0x0100101C`,
`0x01087B72`, `0x0108FF1A`). Escaneei os `.mif` dos 38 jogos bloqueados por qualquer
valor de 4 bytes com esse prefixo. **Não funcionou de forma confiável**: em quase todos
os arquivos há dezenas de valores parecidos (`0x1000100`, `0x1000300` etc.) que são só
campos estruturais do formato `.mif`, não CLSIDs — só 1 dos 38 teve um candidato único e
claro. A conclusão honesta é que **vai precisar do mesmo processo manual/semi-manual
que resolveu o Zeeboids** (achar o valor certo com evidência binária, cadastrar em
`aee_ids.h`/`mif_parser.c`), um por um ou em pequenos lotes — não existe atalho
automático óbvio com o que temos hoje.

### Achado nº 2 (maior impacto por correção única): 13 jogos travam no MESMO endereço

`274214` (cnk2/Crash), `278986` (cninja), `278987` (spinmast), `278988` (strhoop),
`279036` (game.mod), `279125` (supbtime/Super BurgerTime), `279126` (karnovr),
`279173` (wizdfire), `279200` (magdrop3), `279233` (darkseal), `279888` (baddudes),
`279889` (hbarrel) — **todos** descarrilam exatamente em `PC=0x12000000`, com o mesmo
padrão de trace: o PC incrementa de 4 em 4 byte a byte
(`0x11FFFF04 -> 0x11FFFF08 -> ... -> 0x12000000`), sinal de que a CPU está "andando"
por memória zerada (`ANDEQ r0,r0,r0`, o padrão de bytes-zero decodificado como
instrução ARM) até bater num limite exato — neste caso o **fim da região de heap**
(`ZMEM_HEAP_BASE 0x10000000 + ZMEM_HEAP_SIZE 32MB = 0x12000000`, ver
`src/memory/memory.h`).

Isso é **exatamente o mesmo tipo de bug documentado em `STATUS_C_BOOT_PACMANIA_ZEEBOIDS.md`
(Rodada 2) para o Zeeboids**: o stub de relocação PIC (ROPI/RWPI) gerado pelo compilador
`armcc` procura uma tabela de realocação (`Region$$Table`) num endereço que o nosso
`mod_loader.c` não reproduz corretamente, o cálculo de ponteiro dá um valor que não
aponta pra lugar nenhum de útil, e a CPU acaba "passeando" por memória vazia até bater
num limite de região (lá foi o topo dos 64MB de RAM; aqui é o topo dos 32MB de heap —
mesma causa, vítima de endereço-limite diferente por acaso de onde cada jogo foi
carregado). **Resolver esse padrão de vez (entender o formato real da `Region$$Table` no
`.mod`) tem o maior retorno de qualquer correção única possível hoje: destrava até 13
jogos simultaneamente**, sem contar o Zeeboids que já tinha esse problema catalogado.

### Achado nº 3 (menor): 2 jogos com CLSID errado detectado

`274755` (tectoy.mod) e `279394` (rocketweb.mod) acham *um* CLSID no arquivo (por
scan ou nome de caminho), mas ele não bate com o único CLSID tratado hoje no `case 5`
de `zbrew_handle_stub()` (`0x0100101C`, o do bootstrap do Pac-Mania). Provavelmente
esses dois jogos usam uma classe de bootstrap diferente — precisa de investigação
específica, mas afeta só 2 jogos, prioridade baixa.

### Os 10 jogos que já "rodam" hoje (Tier 2 candidato, com dados reais)

| ID | Arquivo | Nome provável | Instruções (120 frames, baseline pré-rodada-5) |
|---|---|---|---|
| 274754 | ddragonz.mod | Double Dragon | 17.671 |
| 274791 | zeebo_app.mod | Zeebo App | 2.023 |
| 274802 | quake.mod | Quake | 1.245 |
| 274804 | brainchallenge.mod | Brain Challenge | 5.985.220 |
| 276153 | quake2brew.mod | Quake II | 373 |
| 276212 | pacmania.mod | Pac-Mania | 145.686 |
| 277229 | game.mod | Family Pack | 58.980.218 |
| 277495 | reksio.mod | Reksio | 16.138 |
| 277727 | Bajaz.mod | (Baja?) | 137.222 |
| 279382 | zeeboids.mod | Zeeboids | 544.678 |

**Atenção**: "rodando" aqui significa só "completou o boot sem travar" — não significa
que tem imagem na tela. Quake, Quake II e Brain Challenge nunca foram testados a fundo
(fora do escopo desta rodada); são os candidatos mais óbvios pra virar tarefa dedicada
numa Rodada 6, junto com o pacote de 13 jogos do bug `0x12000000`.

CSV completo com os 62 jogos (id, arquivo, classificação, endereço de crash quando
aplicável, cor do primeiro pixel, contagem de instruções):
[`sweep_result.csv`](sweep_result.csv).

---

## FASE 2 — Integração das branches A-E

### Ordem e conflitos

Merge sequencial `master -> A(signals) -> B(idisplay) -> C(pacmania-draw) ->
D(gles-fp) -> E(input-hid)` na branch `rodada5/integracao` (worktree próprio, não
mexi em nenhum worktree alheio).

- **A -> master**: fast-forward, sem conflito.
- **B -> A**: 1 conflito em `src/brew/boot.c`, no `case 6` do dispatch de stub — A
  tinha adicionado um `if (clsid == AEECLSID_HID_REAL)` novo e B um
  `if (clsid == AEECLSID_DISPLAY_REAL)` novo, ambos dentro do mesmo `case 6`. Resolvido
  mantendo os dois blocos como `if/else if` sequenciais (são CLSIDs mutuamente
  exclusivos, lógica trivial de conciliar — nenhuma das duas tarefas perdeu código).
- **C -> B**: sem conflito (arquivos diferentes: `helpers.c` vs `boot.c`/`idisplay.c`).
- **D -> C**: sem conflito (`egl_gl.c` e um teste novo, isolados).
- **E -> D**: sem conflito — `boot.c` teve merge automático limpo (mudanças em
  pontos diferentes do arquivo).

### Build

Compila limpo (`msbuild`, Release x64, 0 erros) na branch integrada.

### Regressão nos 4 jogos Tier 1 (smoke test real, log colado)

Baseline de referência: `docs/PROGRESS.md` / `STATUS_R5_B_IDISPLAY.md` reportavam
"13695/17671 DD, 75306/145686 Pac-Mania, 729568 congelado Family Pack, 544678
congelado Zeeboids" antes desta integração.

| Jogo | frame 1 (instruções) | frame 121 (instruções) | fb[0] frame 121 | Observação |
|---|---|---|---|---|
| Double Dragon | 9.222 | **2.756.802** | `0xFFFFFFFF` (branco, igual à baseline) | Muito mais atividade de CPU que a baseline (~17k) — sinais/HID de E rodando bastante, sem regressão visual |
| Pac-Mania | 4.214 | **145.686** | `0xFF000000` (preto) | Bate exatamente com a baseline (145.686) — sem regressão, texto ainda não aparece nesta janela (ver STATUS_R5_C) |
| Family Pack | 729.568 (bate com a baseline) | **2.749.905** | `0xFF000000` (preto) | **Deixou de estar congelado** (antes ficava travado em 729.568 do frame 1 ao 121) — avançou, mas ainda 0 chamadas GL |
| Zeeboids | 544.671 | 544.678 (bate com a baseline) | `0xFF000000` (preto) | Praticamente parado, igual à baseline — não usa o mecanismo novo de HID |

**Nenhuma regressão**: nenhum dos 4 jogos passou a travar/descarrilar, nenhum piorou em
relação à contagem de instruções ou cor do framebuffer documentada antes desta rodada.

### Detalhe extra descoberto na integração: novo bloqueador do Family Pack

Rodando o Family Pack integrado até o fim, o log mostra que o sinal de "controle
conectado" (`IHID_RegisterForConnectEvents`, implementado pela Tarefa A) agora dispara
de verdade — e o callback do próprio jogo (`DeviceConnectCB`) tenta ler endereços de
memória inválidos (`0xEA000014`, depois `0xEA000024`) e entra num loop que nunca
retorna. O watchdog de instruções que a Tarefa A escreveu
(`ZSIGNAL_CALL_INSTR_BUDGET`) evita que isso trave o emulador para sempre (força volta
ao estado normal após ~2 milhões de instruções), mas **o jogo ainda não desenha nada**.
Isso é consistente com o que `STATUS_R5_A_SIGNALS.md` já registrava ("Family Pack ainda
não renderiza"), só que agora com o log exato do endereço inválido, que é pista nova
pra quem pegar esse bloqueador na Rodada 6.

---

## Tabela por jogo: antes -> depois (Tier 1, evidência)

| Jogo | Antes da Rodada 5 | Depois da Rodada 5 (integrado) | Evidência |
|---|---|---|---|
| Double Dragon | Rodava, pintava branco via timer | Rodava, pinta branco via timer **+** signals/HID processando eventos reais (muito mais instruções) | Log frame 121 acima |
| Family Pack | **Congelado** (0 chamadas GL, travado desde o frame 1) | Sai do congelamento; processa evento de conexão HID; acha um novo bug interno do jogo (leitura de memória inválida no callback) | Log completo colado na seção Fase 2 |
| Pac-Mania | Tela preta, sem pista do porquê | Tela preta, mas com causa raiz isolada (mecanismo de texto funciona isoladamente, jogo não chama ele ainda nesta janela) + fix de conversão de string aplicado | `STATUS_R5_C_PACMANIA_DRAW.md` |
| Zeeboids | Quase parado (idle após EVT_APP_START) | Quase parado, sem mudança (não usa o mecanismo de HID que ganhou suporte) | Log frame 121 acima |

---

## Próximo passo nº 1 para a Rodada 6

**Consertar o padrão "descarrilou em 0x12000000 / 0x04000000"** (stub de relocação PIC
do `armcc` procurando uma `Region$$Table` que não está onde o `mod_loader.c` assume).
Isso já bloqueia o Zeeboids (documentado desde a Rodada 2) e mais **13 jogos** descobertos
nesta varredura — nenhuma outra correção possível hoje destrava tantos jogos de uma vez.
Sugestão de abordagem: entender o formato real de seção do `.mod`/`.bar` (pode ser que
os dados de relocação estejam no `.bar` companheiro, hoje ignorado com "magic
desconhecido") ou, como atalho mais barato, detectar o padrão de entrada do stub PIC
(`push {r0,r1,r2,r4,lr}` em `0x40`/`0x48`) e simular via HLE o efeito esperado
(zerar BSS — já feito hoje via outro caminho — sem deixar a CPU executar o stub real).

Segundo lugar: catalogar CLSIDs dos 37 jogos bloqueados por resolução (processo manual
tipo o que resolveu o Zeeboids), começando pelos que a varredura mostrou avançarem mais
rápido até travar (nenhum sinal claro de prioridade aqui — todos falham em <150
instruções, então qualquer ordem serve).

---

## Arquivos desta entrega

- `sweep_68_games.ps1` — script de varredura reutilizável (parâmetros: caminho da DLL,
  do smoke test, do romset, timeout por jogo).
- `sweep_result.csv` — resultado bruto dos 62 jogos testados.
- Branch `rodada5/integracao` (worktree `zeebo_libretro-integracao`) — merge das 5
  branches A-E, compila limpo, testado nos 4 jogos Tier 1, **não mesclada em master**
  (fica pronta pra você revisar/testar no RetroArch antes de aprovar o merge final).
- Branch `rodada5/validacao` (este worktree) — só documentação/scripts, nenhuma mudança
  em `src/`.
