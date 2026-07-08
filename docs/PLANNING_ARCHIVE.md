# Arquivo de Planejamento (Pré-Desenvolvimento)

Este documento arquiva o *raciocínio de design* dos 15 documentos de
planejamento escritos antes de qualquer código existir (pasta `Emulador/`,
incluindo `Emulador/n1/`). Esses documentos misturavam teoria, tutorial de
ferramentas (make/cmake/gdb/git) e um plano de fases semana-a-semana que já
foi executado e substituído pelo estado real do projeto em
`docs/PROGRESS.md`.

Não repita aqui o que está em PROGRESS.md (o que foi feito, estatísticas,
próximos passos). O valor deste arquivo é só o "por quê" por trás de
decisões que não são óbvias olhando o código, e o glossário técnico, que
continua útil como referência standalone.

---

## Arquitetura

### LibRetro como camada fina
A decisão de arquitetura original: o núcleo é "só" um `.dll/.so` que
implementa as funções `retro_*` e chama callbacks (`video_cb`,
`audio_batch_cb`, `input_poll_cb`/`input_state_cb`, `environ_cb`) fornecidos
pelo RetroArch. RetroArch cuida de menu, save states, config e mapeamento de
input — o núcleo só precisa emular CPU, gerar frame de vídeo e buffer de
áudio. Isso não mudou, mas explica por que o core não implementa nada de UI
ou de gerenciamento de arquivos fora do necessário para carregar a ROM.

### Zeebo: por que HLE e não LLE
O Zeebo (TecToy, 2009, Brasil/México) roda sobre BREW (Qualcomm) em CPU
ARM11 (~528MHz, ARMv6). Emulação de baixo nível (LLE) exigiria a ROM do BREW
da Qualcomm, que não está disponível/não é viável legalmente. Por isso a
decisão de projeto, desde o início, foi **HLE puro**: reimplementar o
comportamento das APIs BREW em C, sem tentar rodar o BREW original. Isso é
o mesmo caminho usado pelo Infuse (outro emulador de Zeebo, referência usada
durante o planejamento).

Nota de hardware: o silício real usa ARM11 (ARMv6); alguns emuladores de
referência (Infuse) tratam a CPU como Cortex-A8 (ARMv7) para fins de HLE.
Como o alvo é o comportamento das APIs BREW e não o silício, essa diferença
raramente importa na prática.

### Modelo de interface do BREW (estilo COM)
BREW organiza suas APIs como "interfaces" no estilo COM da Microsoft:
`IShell`, `IDisplay`, `IGraphics`, `ISound`, `IFile`, etc. O jogo pede uma
interface via `IShell_CreateInstance(shell, AEECLSID_X, &pInterface)`, e
depois chama métodos indexando uma vtable (`pInterface->vtable->Metodo(...)`).
Emular BREW em HLE significa recriar esse modelo com vtables falsas que
apontam para código do emulador em vez de código real do BREW — ver seção
de APIs abaixo para o mecanismo de trap escolhido.

### Formato de ROM (MOD/MIF/BAR)
- **MOD** (`.mod`): o executável BREW em si — código ARM + dados. É o
  único arquivo que a CPU realmente executa, e por isso é a única extensão
  registrada em `retro_get_system_info` (`valid_extensions = "mod"`).
- **MIF** (`.mif`): metadados (nome, ícone, class IDs, extensões
  requeridas) — usado só para exibir o jogo no menu, nunca deve entrar na
  lista de extensões carregáveis.
- **BAR** (`.bar`): recursos (imagens, sons, strings, layouts) que o jogo
  carrega em tempo de execução.

A estrutura exata de um MOD não é documentada publicamente e precisou (e
ainda precisa, para jogos reais) de engenharia reversa via Ghidra — isso
não era um detalhe resolvível em fase de planejamento.

---

## Decisões de Design da CPU

### Barrel shifter — implementado desde o início, não depois
Qualquer instrução de Data Processing ARM pode ter seu operando 2
deslocado (shiftado) embutido no encoding, sem custar uma instrução extra
(`ADD R0, R1, R2, LSL #2`). Decisão de projeto: implementar isso desde a
Fase 1, e não como um "extra" posterior, porque compiladores C geram esse
padrão o tempo todo (acesso a arrays, multiplicação por potência de 2) —
qualquer jogo real depende disso constantemente.

Formas do operando 2 (bits 11-0):
- bit 25 = 1 (immediate): `ROR(imm8, rotate*2)`
- bit 25 = 0, bit 4 = 0: registrador com shift por imediato
- bit 25 = 0, bit 4 = 1: registrador com shift por registrador

Tipos de shift (bits 6-5, quando bit 25 = 0): `00=LSL, 01=LSR, 10=ASR
(preserva sinal), 11=ROR` (ou RRX se shift=0 com registrador).

**Detalhe fácil de esquecer — carry via shifter:** quando a instrução tem
o bit S setado E usa shift no operando 2, o *último bit deslocado para
fora* alimenta a flag Carry — mesmo em instruções que não são shifts puros
(MOV/MVN/AND/ORR/EOR/BIC com shift + S). Isso é diferente do carry setado
por ADD/SUB (que vem da soma). Ex.: `MOVS R0, R1, LSL #1` → C = bit 31 de
R1 (o bit que "saiu"). Decisão: implementar isso junto com o barrel
shifter desde o início, pois os dois andam juntos no hardware.

### Constantes imediatas rotacionadas
ARM só reserva 12 bits para representar uma constante imediata em Data
Processing, mas jogos usam valores de 32 bits o tempo todo. Encoding: 4
bits de "rotate" (0-15) + 8 bits de valor (`imm8`). Fórmula:
`valor = ROR(imm8, rotate*2)`. Decisão: implementar a fórmula exata desde a
Fase 1 — simplificar para "ler 12 bits direto" quebraria qualquer
`MOV`/`CMP`/etc com constante grande (ex.: `MOV R0, #0xFF000000` tem
imm8=0xFF, rotate=4).

### Tabela completa das 16 condições
Usar a tabela ARM exata, não improvisar — em particular `HI`/`LS`/`GT`/`LE`
são fáceis de implementar errado porque combinam duas flags:

| Código | Mnem. | Condição lógica |
|---|---|---|
| 0000 | EQ | Z==1 |
| 0001 | NE | Z==0 |
| 0010 | CS/HS | C==1 |
| 0011 | CC/LO | C==0 |
| 0100 | MI | N==1 |
| 0101 | PL | N==0 |
| 0110 | VS | V==1 |
| 0111 | VC | V==0 |
| 1000 | HI | C==1 AND Z==0 |
| 1001 | LS | C==0 OR Z==1 |
| 1010 | GE | N==V |
| 1011 | LT | N!=V |
| 1100 | GT | Z==0 AND N==V |
| 1101 | LE | Z==1 OR N!=V |
| 1110 | AL | sempre |
| 1111 | NV | nunca executa (reservado/unpredictable em ARM moderno; tratado como "nunca" por segurança em HLE) |

### Pipeline: não simular de verdade, mas simular o efeito sobre o PC
No ARM real, PC aponta 8 bytes à frente da instrução em execução (modo
ARM) ou 4 bytes (modo Thumb), por causa do pipeline de 3 estágios do
hardware. Decisão: **não** simular um pipeline de verdade (seria complexo e
desnecessário em HLE), **mas** simular o efeito sobre o valor de PC quando
lido como operando (`MOV R0, PC` / `ADD R0, PC, R1`):
`valor_PC_como_operando = PC_real + 8` (ARM) ou `+4` (Thumb). Isso é
essencial para branches relativos calculados corretamente sem precisar de
um pipeline real.

### Escopo: apenas modo User, sem bancos de registrador
O ARM real tem vários modos de operação (User, IRQ, FIQ, Supervisor,
Abort, Undefined), cada um com seu próprio banco de registradores.
Decisão: emular **apenas o modo User**, com um único banco de 16
registradores. Motivo: HLE substitui inteiramente o "sistema operacional"
BREW por código HLE em C, então não existe IRQ/FIQ/Supervisor real do
hardware original para emular. Se o CPSR indicar troca de modo, isso é
logado como aviso e ignorado — não implementado.

### Erros nunca travam o núcleo
Princípio central adotado desde o planejamento e que orienta bastante do
código atual: **nunca travar o emulador silenciosamente**.
- Instrução desconhecida/não implementada → loga
  `"Unknown instruction: 0x%08X at PC=0x%X"`, trata como NOP, continua.
- Acesso de memória fora dos limites → loga aviso com endereço e PC,
  retorna 0 (leitura) ou ignora (escrita) — nunca trava o processo.
- Acesso desalinhado (ex.: LDR 32-bit em endereço não múltiplo de 4) → é
  **permitido**, feito byte a byte mesmo desalinhado; loga aviso só na
  primeira ocorrência de cada endereço (evita inundar o log); **não**
  implementa a rotação de valor que o hardware real faz em alguns modos —
  considerado comportamento raro o suficiente para não valer a
  complexidade.

Justificativa registrada no planejamento: erros devem ficar visíveis nos
logs para facilitar debug, mas travar o RetroArch por causa de uma
instrução não suportada é sempre pior do que continuar com comportamento
levemente incorreto e permitir investigar depois.

### Fora de escopo (decisões fechadas, não esquecimentos)
- **Coprocessador** (CDP/MCR/MRC): Zeebo não usa coprocessador
  customizado em jogos comuns — tratado como instrução desconhecida se
  aparecer, sem handler dedicado.
- **Thumb-2**: ARM11/ARMv6 suporta Thumb clássico (16-bit) mas não o
  conjunto estendido Thumb-2 (ARMv6T2+). Se aparecer um encoding que
  pareça Thumb-2, é tratado como instrução desconhecida.
- **Interpretador antes de JIT**: a estratégia sempre foi começar com um
  interpretador simples (fácil de debugar, portável) e só considerar JIT
  depois, caso performance vire um problema real — não antes.

---

## Decisões de Design das APIs BREW

### Mecanismo de trap: endereços mágicos, não SWI
Havia duas opções para interceptar quando o jogo "chama" uma API BREW
através da vtable falsa:
1. **Endereços mágicos**: colocar valores impossíveis (ex.: `0xF0000001`)
   nas entradas da vtable; quando a CPU tenta "executar" um PC nessa
   faixa, o emulador intercepta antes de buscar uma instrução real.
2. **Instrução SWI/SVC**: cada API teria um número de software interrupt
   diferente.

**Decisão: endereços mágicos.** Motivos registrados no planejamento:
- Mais simples no loop principal de fetch — é só um `if (PC >= faixa)`,
  sem precisar decodificar corretamente mais um tipo de instrução.
- Evita depender de reconhecer o encoding de SWI/SVC nos dois modos (ARM e
  Thumb têm encodings diferentes para isso).
- É o modelo mais comum em emuladores HLE de plataformas com vtables/BIOS
  baseada em interfaces estilo COM.

SWI continua sendo decodificado pela CPU (por completude), mas seu uso na
prática é esperado ser raro; se aparecer, é logado como não implementado
em vez de ser parte do mecanismo principal de trap.

### Convenção de chamada (como ler argumentos de uma API)
Ao emular uma API BREW, os argumentos chegam pela convenção de chamada ARM
padrão: R0-R3 para os primeiros 4 argumentos (R0 é tipicamente o ponteiro
`this` da interface), argumentos extras na stack, retorno em R0. Toda
implementação de método HLE segue esse padrão para ler seus parâmetros.

### Por que HLE (reforço)
Ver seção "Arquitetura" acima — a escolha de HLE sobre LLE é a decisão
mais fundamental de todo o projeto e afeta todo o design das APIs: nunca
se tenta reproduzir o BREW bit a bit, só o comportamento observável que os
jogos esperam.

---

## Mapeamento de Memória — Racional de Design

O mapa de memória proposto na fase de planejamento (regiões de 256MB para
código+dados, heap, stack, e uma região "mágica" em `0xF0000000+` para
trap de APIs) foi só uma primeira proposta de tamanhos; a implementação
real usa números menores (ver `docs/PROGRESS.md`: RAM 64MB, heap 32MB,
stack 4MB, VRAM 2MB). Os *números* mudaram, mas os *princípios* de design
por trás do mapa se mantiveram e vale registrar o porquê:

- **Bounds checking nunca trava**: leitura fora dos limites loga aviso e
  retorna 0; escrita fora dos limites loga aviso e é ignorada. Nunca
  propaga um erro que derrube o núcleo — mesma filosofia da seção de CPU
  acima, e por design as duas foram pensadas para serem consistentes entre
  si.
- **Acesso desalinhado é permitido**, não rejeitado — loga aviso (uma vez
  por endereço) mas não simula a rotação de valor que o hardware real faz
  em modos específicos, por ser um caso raro para os jogos-alvo.
- **Região HLE em endereços altos** (topo do espaço de 32 bits) foi
  escolhida deliberadamente para não colidir com o espaço de endereços
  normalmente usado por código/dados/heap/stack do jogo — é uma
  convenção pura do emulador, sem correspondência com hardware real.
- A CPU não aloca fisicamente 4GB: aloca só o necessário (buffer físico
  menor) e traduz endereço virtual → posição no buffer, checando a região
  antes de cada acesso (código/dados vs heap vs stack vs HLE).

---

## Glossário Técnico

Termos que não têm equivalente já documentado em outro lugar do projeto.

**LibRetro / RetroArch**
- **LibRetro**: interface padronizada que permite qualquer emulador
  ("núcleo") rodar dentro do RetroArch.
- **Núcleo (Core)**: o emulador compilado como `.so`/`.dll`/`.dylib` que o
  RetroArch carrega.
- **Callback**: função que uma parte do código "empresta" para outra
  chamar (ex.: RetroArch dá `video_cb`, o núcleo chama quando quer mostrar
  uma imagem).

**Hardware / CPU (ARM)**
- **RISC**: filosofia de CPU com instruções simples e de tamanho fixo
  (oposto de CISC/x86).
- **PC / SP / LR**: R15 (próxima instrução), R13 (topo da stack), R14
  (endereço de retorno de função).
- **CPSR**: registrador de flags (N/Z/C/V) e modo atual da CPU.
- **Opcode**: valor numérico que identifica qual instrução executar.
- **Fetch-Decode-Execute**: ciclo básico de qualquer CPU (real ou
  emulada) — ler, entender, executar a instrução.
- **Little-endian**: byte menos significativo primeiro na memória (ARM e
  Zeebo usam isso).
- **Interpretador vs JIT**: interpretador lê/executa uma instrução por
  vez (simples, mais lento); JIT traduz blocos para código nativo do host
  (rápido, muito mais complexo) — fica para fases avançadas.

**Zeebo / BREW**
- **BREW**: plataforma da Qualcomm que roda no Zeebo, fornecendo as APIs
  que os jogos usam (como um SO simplificado para aplicativos).
- **Interface (BREW)**: conjunto de funcionalidades que o jogo pode pedir
  ao sistema (IDisplay, ISound, IFile...), modelo parecido com COM.
- **vtable**: tabela de ponteiros de função usada para implementar uma
  interface; o jogo "chama um método" seguindo um ponteiro dentro dela.
- **Class ID**: número único que identifica qual interface BREW um jogo
  está pedindo (ex.: `AEECLSID_DISPLAY`).
- **Trap (mecanismo de)**: técnica do emulador para interceptar quando o
  jogo tenta chamar uma API BREW, redirecionando para o código HLE.
- **Applet**: o "aplicativo" BREW em execução — o jogo, do ponto de vista
  do sistema.
- **AEEMod_Load**: função de entrada padrão que o BREW chama para
  carregar um módulo (jogo).

**Conceitos gerais de emulação**
- **Bounds checking**: verificação de que um acesso de memória está
  dentro dos limites válidos, evitando crashes silenciosos.
- **Alinhamento**: regra de que acessos de 16/32-bit "deveriam" começar em
  endereços múltiplos de 2/4 (no hardware real; este projeto permite
  acesso desalinhado mesmo assim — ver seção de memória acima).
- **QEMU**: emulador de CPU genérico, usado durante o planejamento como
  possível referência de baixo nível para comparar comportamento de
  instruções ARM (ideia de validação, não uma dependência do projeto).
