# 🕹️ Arquitetura Zeebo - Hardware Detalhado

> O que exatamente você está emulando

---

## 📌 O Que é o Zeebo?

O Zeebo foi um console digital-only lançado pela TecToy em 2009, no Brasil e México. Baseado na plataforma **Qualcomm BREW** (Binary Runtime Environment for Wireless), usava hardware de celular da época.

```
Especificações:
├─ CPU: Qualcomm MSM7201A (ARM11, ~528 MHz)
├─ GPU: Adreno 130 (integrada)
├─ RAM: ~100MB disponível para jogos
├─ Armazenamento: 1GB interno + downloads
├─ Resolução: 640x480 (saída TV)
└─ SO: BREW (Qualcomm)
```

**IMPORTANTE:** Nota sobre a CPU
- O hardware real usa ARM11 (ARMv6)
- Alguns emuladores (como Infuse) usam Cortex-A8 (ARMv7) como referência HLE
- Para HLE, o importante é emular o **comportamento das APIs BREW**, não replicar exatamente o silício

---

## 🧠 CPU: O Coração do Emulador

### Arquitetura ARM

```
O Zeebo usa um processador ARM.
ARM é uma arquitetura RISC (Reduced Instruction Set Computer).

Características:
├─ Instruções de tamanho fixo (32-bit ARM, 16-bit Thumb)
├─ 16 registradores de propósito geral (R0-R15)
├─ Load/Store architecture (memória só via LDR/STR)
├─ Execução condicional (qualquer instrução pode ser condicional)
└─ Pipeline (fetch, decode, execute)
```

### Registradores

```
R0-R12  : Registradores de propósito geral
R13 (SP): Stack Pointer (topo da pilha)
R14 (LR): Link Register (endereço de retorno de funções)
R15 (PC): Program Counter (próxima instrução)

CPSR    : Current Program Status Register (flags e modo)
SPSR    : Saved Program Status Register (backup do CPSR)
```

### Detalhamento dos Registradores

```
┌────────┬─────────────────────────────────────────┐
│ Reg    │ Uso Típico                              │
├────────┼─────────────────────────────────────────┤
│ R0     │ Argumento 1 / Valor de retorno          │
│ R1     │ Argumento 2                             │
│ R2     │ Argumento 3                             │
│ R3     │ Argumento 4                             │
│ R4-R11 │ Variáveis locais (preservadas)         │
│ R12    │ Scratch register (temporário)          │
│ R13 SP │ Stack Pointer                          │
│ R14 LR │ Return address                         │
│ R15 PC │ Program Counter                        │
└────────┴─────────────────────────────────────────┘
```

### CPSR - Flags

```
Bit 31 (N): Negative  - resultado foi negativo
Bit 30 (Z): Zero      - resultado foi zero
Bit 29 (C): Carry     - houve carry/borrow
Bit 28 (V): Overflow  - houve overflow

Bit 7  (I): IRQ disable
Bit 6  (F): FIQ disable
Bit 5  (T): Thumb mode - 1=Thumb, 0=ARM
Bits 4-0  : Modo (User, System, IRQ, etc)
```

### Modos de Operação (referência do hardware real)

```
User (0x10)      : Modo normal de execução (jogos rodam aqui)
System (0x1F)    : Modo privilegiado com registradores de User
Supervisor (0x13): Modo do SO (após reset ou SWI)
IRQ (0x12)       : Interrupções normais
FIQ (0x11)       : Interrupções rápidas
Abort (0x17)     : Falha de acesso à memória
Undefined (0x1B) : Instrução indefinida

DECISÃO DE ESCOPO (ver 04_EMULACAO_CPU_DETALHADA.md):
Este projeto emula APENAS o modo User, com um único banco de
16 registradores. Os outros modos existem no hardware real para
tratar interrupções e chamadas de sistema do BREW original, mas
em HLE isso não é necessário: o "sistema operacional" é
substituído inteiramente pelo código HLE em C, então não há
IRQ/FIQ/Supervisor real para emular. Se o CPSR indicar mudança
de modo, isso é logado como aviso e ignorado - não é implementado.
```

---

## 🗺️ Mapa de Memória

O Zeebo tem espaços de memória mapeados. Esta é uma **aproximação para HLE** (o mapa físico exato do hardware não é totalmente público):

```
┌──────────────┬──────────────────────────────────────┐
│ Endereço     │ Uso                                  │
├──────────────┼──────────────────────────────────────┤
│ 0x00000000   │ Vetores de exceção / boot            │
│ 0x00001000   │ Início do código do jogo (típico)    │
│ ...          │ Código executável (.text)            │
│ ...          │ Dados constantes (.rodata)           │
│ ...          │ Dados inicializados (.data)          │
│ ...          │ Dados não inicializados (.bss)       │
│ ...          │ Heap (cresce para cima)              │
│ ...          │ ... espaço livre ...                 │
│ (topo)       │ Stack (cresce para baixo)            │
├──────────────┼──────────────────────────────────────┤
│ Regiões HLE  │ (Não existem no hardware, são        │
│ especiais    │  convenções do emulador para         │
│              │  interceptar chamadas BREW)          │
└──────────────┴──────────────────────────────────────┘

NOTA: Em HLE, você define este mapa. O importante é ser
consistente e ter espaço suficiente para código+dados+heap+stack.
```

### Layout Recomendado para o Emulador (HLE)

```
0x00000000 - 0x00000FFF : Reservado (vetores)
0x00001000 - 0x0FFFFFFF : Código + Dados do jogo (~256MB)
0x10000000 - 0x1FFFFFFF : Heap (alocação dinâmica)
```

**NOTA:** Esta é a visão resumida. O mapa DETALHADO e AUTORITATIVO,
com a subdivisão exata entre código e dados dentro dessa faixa, e
as decisões fechadas sobre bounds checking/alinhamento, está em
`09_MAPEAMENTO_MEMORIAS.md` - use aquele documento como referência
durante a implementação, este aqui é só a visão geral.
0x20000000 - 0x2FFFFFFF : Stack (cresce para baixo do topo)
0xF0000000 - 0xFFFFFFFF : Região "mágica" HLE (trap de APIs)

Esta é uma decisão de DESIGN sua. Você escolhe.
Vantagem: simples, endereços altos para HLE não colidem com jogo.
```

---

## 🎨 Sistema Gráfico

### Como Zeebo Renderiza

```
BREW oferece APIs de gráficos:
├─ IDisplay   : Controle da tela
├─ IGraphics  : Desenho 2D
├─ IBitmap    : Imagens/sprites
└─ (OpenGL ES): Para jogos 3D (Crash, etc)

Resolução de saída: 640x480 (para TV)
Formato de cor: tipicamente RGB565 ou RGBA8888
```

### Para o Emulador

```
Você vai emular via HLE:
├─ Interceptar chamadas IDisplay/IGraphics
├─ Manter um framebuffer (640x480 pixels)
├─ Quando jogo desenha, você atualiza framebuffer
└─ Enviar framebuffer ao RetroArch via video_cb()

Framebuffer:
uint32_t framebuffer[640 * 480];  // RGBA, 1 pixel = 4 bytes
```

### Jogos 2D vs 3D

```
2D (Double Dragon, Family Pack):
├─ Usam IGraphics/IBitmap
├─ Desenham sprites e tiles
├─ Mais fácil de emular
└─ Comece por estes

3D (Crash Nitro Kart):
├─ Usam OpenGL ES 1.x
├─ Precisam emular pipeline 3D
├─ Mais complexo
└─ Deixe para depois (ou use OpenGL do host)
```

---

## 🔊 Sistema de Áudio

### Como Zeebo Toca Som

```
BREW oferece APIs de áudio:
├─ ISound      : Efeitos sonoros
├─ ISoundPlayer: Player de música
├─ IMedia      : Reprodução de mídia

Formatos suportados:
├─ PCM   : Áudio raw (samples)
├─ ADPCM : Áudio comprimido
├─ MIDI  : Música sequenciada
└─ MP3   : Música comprimida
```

### Para o Emulador

```
Você vai emular via HLE:
├─ Interceptar chamadas ISound/IMedia
├─ Decodificar o formato (PCM/MIDI/MP3)
├─ Mixar em um buffer de áudio
├─ Enviar ao RetroArch via audio_batch_cb()

Sample rate: 44100 Hz (padrão)
Canais: 2 (stereo)
Formato: 16-bit signed
```

### Complexidade por Formato

```
PCM   : Fácil - só copiar samples
ADPCM : Médio - decodificar primeiro
MP3   : Médio - usar biblioteca (minimp3)
MIDI  : Difícil - precisa sintetizador + soundfont

Ordem sugerida: PCM → MP3 → ADPCM → MIDI
```

---

## 🎮 Input (Controle)

### Botões do Zeebo

```
Controle Zeebo:
├─ D-Pad: UP, DOWN, LEFT, RIGHT
├─ Botões de ação: A, B, C (ou similares)
├─ Botões de sistema: START/MENU, SELECT/BACK
└─ (alguns jogos usam o "Boomerang", periférico especial)
```

### Para o Emulador

```
Você vai mapear via HLE:
├─ Interceptar chamadas de input (IHID/eventos de tecla BREW)
├─ Ler estado do controle via input_state_cb() do RetroArch
├─ Traduzir botões RetroArch → eventos BREW que o jogo espera

Mapeamento típico:
RetroArch D-Pad    → Zeebo D-Pad
RetroArch A/B/X/Y  → Zeebo A/B/C
RetroArch Start    → Zeebo Menu
RetroArch Select   → Zeebo Back
```

---

## 📦 Formato de ROM (MOD/MIF)

### O Que São os Arquivos

```
MOD (.mod):
├─ O executável BREW do jogo (o "programa")
├─ Contém código ARM + dados
└─ É o que a CPU executa

MIF (.mif):
├─ Module Information File
├─ Metadados: nome, ícone, class IDs, extensões requeridas
└─ Usado para exibir o jogo no menu

BAR (.bar):
├─ BREW Archive (recursos)
├─ Imagens, strings, sons, layouts
└─ Assets que o jogo carrega
```

### Estrutura Aproximada de um MOD

```
Um MOD é essencialmente um executável para BREW.
A estrutura exata requer reverse engineering, mas em geral:

┌─────────────────┐
│ Header          │ ← Informações do módulo
├─────────────────┤
│ Code (.text)    │ ← Instruções ARM
├─────────────────┤
│ Read-only data  │ ← Constantes
├─────────────────┤
│ Data (.data)    │ ← Variáveis inicializadas
├─────────────────┤
│ BSS (implícito) │ ← Variáveis zeradas
└─────────────────┘

Entry point: onde a execução começa (função AEEMod_Load ou similar)
```

**NOTA:** O formato exato precisa ser estudado com Ghidra/reverse engineering. Este é o tipo de detalhe que você descobre na fase de implementação, não no planejamento.

---

## 🔑 BREW: O Sistema Operacional

### O Que é BREW

```
BREW (Binary Runtime Environment for Wireless):
├─ Plataforma da Qualcomm para celulares
├─ Fornece APIs para aplicativos (jogos)
├─ Baseado em "interfaces" (estilo COM da Microsoft)
└─ Jogos chamam essas APIs para fazer tudo
```

### Como Funciona (Modelo de Interface)

```
BREW usa um modelo tipo COM:

1. Jogo pede uma interface:
   IShell_CreateInstance(shell, AEECLSID_DISPLAY, &pDisplay)

2. BREW retorna um ponteiro para a interface

3. Jogo chama métodos via vtable:
   IDISPLAY_Update(pDisplay)  // Na verdade: pDisplay->vtable->Update(pDisplay)

Para emular (HLE):
├─ Você intercepta IShell_CreateInstance
├─ Retorna suas próprias implementações
└─ Quando jogo chama métodos, você emula o comportamento
```

### Por Que HLE é o Caminho

```
LLE (Low-Level Emulation):
├─ Emular o BREW real, bit por bit
├─ Precisaria da ROM do BREW (não disponível/legal)
├─ Muito complexo
└─ NÃO É VIÁVEL

HLE (High-Level Emulation):
├─ Você reimplementa as APIs BREW
├─ Não precisa da ROM do BREW
├─ Você controla o comportamento
├─ É o que Infuse faz
└─ ESTE É O CAMINHO ✅
```

---

## 🎯 O Que Você Precisa Emular (Resumo)

```
PARA OS 3 PRIMEIROS JOGOS:

1. CPU ARM
   ├─ Interpretador de instruções ARM + Thumb
   ├─ ~50-100 instruções mais comuns
   └─ Flags e execução condicional

2. Memória
   ├─ Read/Write (8, 16, 32-bit)
   ├─ Heap (malloc/free BREW)
   └─ Stack

3. BREW APIs (mínimo)
   ├─ IShell (criação de instâncias)
   ├─ IDisplay/IGraphics (desenho)
   ├─ ISound (áudio básico)
   ├─ IFile (carregar assets)
   └─ Memória (MALLOC/FREE)

4. Gráficos
   ├─ Framebuffer 640x480
   └─ Desenho de sprites/retângulos

5. Áudio
   ├─ PCM playback
   └─ Buffer de mixagem

6. Input
   └─ Mapeamento de botões

PARA 50+ JOGOS (depois):
   ├─ Mais instruções ARM (edge cases)
   ├─ OpenGL ES (jogos 3D)
   ├─ MIDI/MP3 (áudio completo)
   ├─ INet (rede/multiplayer)
   ├─ Mais APIs BREW
   └─ Quirks específicos por jogo
```

---

## 📊 Complexidade dos Componentes

```
┌────────────────────┬─────────────┬──────────────┐
│ Componente         │ Dificuldade │ Prioridade   │
├────────────────────┼─────────────┼──────────────┤
│ CPU Interpreter    │ Alta        │ 1 (primeiro) │
│ Memory Manager     │ Baixa       │ 1 (primeiro) │
│ ROM Loader         │ Média       │ 2            │
│ BREW IShell        │ Média       │ 2            │
│ IDisplay/Graphics  │ Média       │ 3            │
│ IFile              │ Baixa       │ 3            │
│ ISound (PCM)       │ Média       │ 4            │
│ Input              │ Baixa       │ 4            │
│ OpenGL ES (3D)     │ Muito Alta  │ 6 (depois)   │
│ MIDI Synthesis     │ Alta        │ 6 (depois)   │
│ INet               │ Alta        │ 7 (depois)   │
└────────────────────┴─────────────┴──────────────┘
```

---

## 🔗 Referências para Estudar

```
Hardware Zeebo:
├─ https://www.tripleoxygen.net/ (docs Zeebo/BREW)
├─ Emulation General Wiki (Infuse page)
└─ ResetEra/Famiboards threads

ARM Architecture:
├─ ARM Architecture Reference Manual (ARMv6/ARMv7)
├─ ARM11 Technical Reference Manual
└─ Cortex-A8 TRM (referência HLE Infuse)

BREW:
├─ BREW SDK documentation (arquivada)
├─ Qualcomm BREW API reference
└─ Código do Infuse (github.com/Tuxality/Infuse)
```

---

## 🎯 Próximo Passo

Agora que você entende o hardware Zeebo:

→ Leia **03_PLANO_DESENVOLVIMENTO.md** para ver como implementar tudo isso em fases.

Você entende O QUE emular. O próximo doc explica COMO e QUANDO.
