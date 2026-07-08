# 📅 Plano de Desenvolvimento - Núcleo LibRetro Zeebo

> Como implementar tudo, em fases, com timeline realista

---

## 🎯 Objetivo em Camadas

```
META 1 (curto prazo): Núcleo compila e carrega no RetroArch
META 2 (médio prazo): 3 jogos rodando (paridade com Infuse)
META 3 (longo prazo): 50+ jogos (máxima compatibilidade)
```

---

## 🗺️ Visão Geral das Fases

```
FASE 0: Setup & Estrutura        (Semana 1-2)
FASE 1: CPU Core                 (Semana 3-8)
FASE 2: Memória & Loader         (Semana 6-10)
FASE 3: BREW HLE Básico          (Semana 9-14)
FASE 4: Gráficos & Áudio         (Semana 13-18)
FASE 5: Primeiro Jogo            (Semana 17-22)
FASE 6: Os 3 Jogos               (Semana 20-28)
FASE 7: Expansão (50+ jogos)     (Mês 7+)

NOTA: Fases se sobrepõem. Você não termina uma para começar outra.
```

---

## 📋 FASE 0: Setup & Estrutura (Semana 1-2)

### Objetivo
Ter ambiente pronto e esqueleto do projeto que compila.

### Tarefas

```
Setup:
[ ] Instalar toolchain (gcc/clang, make/cmake)
[ ] Baixar libretro.h (header da API)
[ ] Baixar RetroArch para testar
[ ] Configurar editor/IDE

Estrutura:
[ ] Criar estrutura de pastas
[ ] Criar Makefile/CMakeLists
[ ] Criar esqueleto do núcleo (todas as funções retro_* vazias)
[ ] Compilar para .dll/.so
[ ] Carregar no RetroArch (deve aparecer o núcleo, mesmo sem fazer nada)
```

### Entregável
```
✅ zeebo_libretro.dll/.so que RetroArch reconhece
✅ Todas funções retro_* existem (mesmo que vazias)
✅ Compilação sem erros
```

### Checkpoint
"RetroArch mostra 'Zeebo' na lista de núcleos"

---

## 📋 FASE 1: CPU Core (Semana 3-8)

### Objetivo
Emulador ARM que executa instruções.

### Sub-fases

#### 1.1 Estrutura da CPU (Semana 3)
```
[ ] Definir struct da CPU (registradores, flags, PC)
[ ] Funções init/reset/destroy
[ ] Framework de fetch-decode-execute
```

#### 1.2 Primeiras Instruções (Semana 4)
```
[ ] MOV (mover valor)
[ ] ADD, SUB (aritmética)
[ ] Testes unitários de cada uma
```

#### 1.3 Load/Store (Semana 5)
```
[ ] LDR (ler da memória)
[ ] STR (escrever na memória)
[ ] LDRB, STRB (versão byte)
[ ] LDM, STM (múltiplos registradores)
```

#### 1.4 Controle de Fluxo (Semana 6)
```
[ ] B (branch/pulo)
[ ] BL (branch com link/chamada de função)
[ ] BX (branch exchange)
[ ] Execução condicional (flags)
```

#### 1.5 Thumb Mode (Semana 7)
```
[ ] Detectar modo Thumb (bit T do CPSR)
[ ] Decodificar instruções Thumb (16-bit)
[ ] Instruções Thumb essenciais
[ ] Troca ARM ↔ Thumb (BX)
```

#### 1.6 Lógica & Shifts (Semana 8)
```
[ ] AND, ORR, EOR (lógica)
[ ] LSL, LSR, ASR, ROR (shifts)
[ ] CMP, TST (comparações)
[ ] MUL (multiplicação)
```

### Entregável
```
✅ CPU que executa ~50 instruções ARM
✅ CPU que executa instruções Thumb básicas
✅ Testes que validam cada instrução
```

### Checkpoint
"CPU executa um programa ARM de teste corretamente"

---

## 📋 FASE 2: Memória & Loader (Semana 6-10)

### Objetivo
Sistema de memória e capacidade de carregar ROMs.

### Sub-fases

#### 2.1 Memory Manager (Semana 6-7)
```
[ ] Read/Write 8/16/32-bit
[ ] Little-endian handling
[ ] Bounds checking (detectar acessos inválidos)
[ ] Mapa de memória (código, heap, stack)
```

#### 2.2 ROM Loader (Semana 8-9)
```
[ ] Abrir e ler arquivo MOD
[ ] Parse do header (reverse engineering necessário)
[ ] Identificar entry point
[ ] Carregar código na memória
[ ] Carregar dados
```

#### 2.3 MIF/BAR Parser (Semana 10)
```
[ ] Parse MIF (metadados)
[ ] Extrair nome, ícone, class IDs
[ ] Parse BAR (recursos)
[ ] Extrair imagens/strings/sons
```

### Entregável
```
✅ ROM carrega na memória
✅ CPU começa a executar do entry point
✅ Metadados do jogo são lidos
```

### Checkpoint
"ROM real carrega e CPU começa a executar (mesmo que trave logo)"

---

## 📋 FASE 3: BREW HLE Básico (Semana 9-14)

### Objetivo
Implementar APIs BREW essenciais.

### Sub-fases

#### 3.1 Sistema de Trap (Semana 9-10)
```
[ ] Mecanismo para interceptar chamadas BREW
[ ] Quando jogo chama endereço "mágico", você intercepta
[ ] Roteamento para a API correta
[ ] Logging de "API não implementada"
```

#### 3.2 IShell (Semana 11)
```
[ ] IShell_CreateInstance (criar interfaces)
[ ] IShell_GetDeviceInfo
[ ] Gerenciamento de instâncias
```

#### 3.3 Memória BREW (Semana 12)
```
[ ] MALLOC (alocar)
[ ] FREE (liberar)
[ ] REALLOC
[ ] MEMSET, MEMCPY
```

#### 3.4 IFile (Semana 13)
```
[ ] IFileMgr_OpenFile
[ ] IFile_Read
[ ] IFile_Write
[ ] IFile_Close
```

#### 3.5 Applet Lifecycle (Semana 14)
```
[ ] AEEMod_Load (carregar módulo)
[ ] Handler de eventos (EVT_APP_START, etc)
[ ] Event loop
```

### Entregável
```
✅ Jogo consegue criar interfaces
✅ Jogo consegue alocar memória
✅ Jogo consegue ler arquivos
✅ Sistema de trap funciona
```

### Checkpoint
"Jogo passa da inicialização, chega no loop principal"

---

## 📋 FASE 4: Gráficos & Áudio (Semana 13-18)

### Objetivo
Ver e ouvir o jogo.

### Sub-fases

#### 4.1 Framebuffer (Semana 13-14)
```
[ ] Buffer 640x480 RGBA
[ ] Função de limpar tela
[ ] Enviar para RetroArch (video_cb)
```

#### 4.2 IDisplay/IGraphics (Semana 15-16)
```
[ ] IDisplay_Update (atualizar tela)
[ ] IGraphics_DrawRect
[ ] IGraphics_DrawLine
[ ] IGraphics_SetColor
```

#### 4.3 IBitmap (Semana 16-17)
```
[ ] Carregar bitmap
[ ] Desenhar bitmap (blit)
[ ] Transparência
```

#### 4.4 Áudio PCM (Semana 17-18)
```
[ ] Buffer de áudio
[ ] ISound_Play (PCM)
[ ] Mixer básico
[ ] Enviar para RetroArch (audio_batch_cb)
```

### Entregável
```
✅ Tela mostra algo (mesmo que buggy)
✅ Sons básicos tocam
✅ Frame é enviado ao RetroArch
```

### Checkpoint
"Aparece imagem na tela do RetroArch"

---

## 📋 FASE 5: Primeiro Jogo (Semana 17-22)

### Objetivo
UM jogo rodando (mesmo que imperfeito).

### Estratégia

```
Escolher o jogo mais simples:
└─ Zeebo Family Pack (mini-games simples)

Processo iterativo:
1. Rodar o jogo
2. Ver onde trava (log "Unimplemented")
3. Implementar o que falta
4. Repetir até rodar

Cada trava = 1 API ou 1 instrução faltando
```

### Tarefas
```
[ ] Rodar Family Pack
[ ] Implementar instruções faltando
[ ] Implementar APIs faltando
[ ] Corrigir bugs de renderização
[ ] Corrigir bugs de input
[ ] Jogo fica jogável
```

### Entregável
```
✅ 1 jogo jogável do início ao fim
```

### Checkpoint
"Consigo jogar Family Pack no RetroArch"

---

## 📋 FASE 6: Os 3 Jogos (Semana 20-28)

### Objetivo
Paridade com Infuse (3 jogos jogáveis).

### Jogos Alvo
```
1. Zeebo Family Pack  (já feito na Fase 5)
2. Double Dragon      (adicionar áudio MIDI, sprites)
3. Crash Nitro Kart   (adicionar 3D/OpenGL ES)
```

### Tarefas por Jogo

#### Double Dragon
```
[ ] Sprites 2D funcionando
[ ] Áudio MIDI (ou stub inicial)
[ ] Input de fighting game
[ ] Corrigir quirks específicos
```

#### Crash Nitro Kart
```
[ ] OpenGL ES básico (ou wrapper para OpenGL do host)
[ ] Rendering 3D
[ ] Texturas
[ ] Performance (3D é pesado)
```

### Entregável
```
✅ 3 jogos jogáveis (paridade com Infuse)
```

### Checkpoint
"Os mesmos 3 jogos do Infuse rodam no meu núcleo"

---

## 📋 FASE 7: Expansão (Mês 7+)

### Objetivo
50+ jogos.

### Estratégia

```
Metodologia de "atacar a lista":

1. Pegar próximo jogo da lista
2. Rodar
3. Debugar até rodar
4. Documentar o que faltava
5. Próximo jogo

Cada jogo novo:
├─ Pode precisar de 1-2 APIs novas
├─ Pode ter quirks específicos
├─ Pode revelar bugs no CPU
└─ Melhora compatibilidade geral
```

### Jogos para Expandir
```
Prioridade Alta (mais simples):
├─ Asphalt Urban GT
├─ Tetris
├─ Zenonia

Prioridade Média:
├─ Action Hero 3D
├─ Zeebo Extreme series
├─ Quake / Quake II

Prioridade Baixa (complexos):
├─ Resident Evil 4
├─ Jogos com rede
└─ Jogos com periféricos especiais
```

### Melhorias Paralelas
```
[ ] Performance (considerar JIT)
[ ] Áudio completo (MIDI synthesis)
[ ] OpenGL ES completo
[ ] INet (multiplayer)
[ ] Save states
```

### Entregável
```
✅ 50+ jogos rodando
✅ Compatibilidade máxima
```

---

## 📊 Timeline Consolidada

```
┌─────────┬────────────────────────────┬──────────────┐
│ Semana  │ Foco                       │ Marco        │
├─────────┼────────────────────────────┼──────────────┤
│ 1-2     │ Setup + Estrutura          │ Núcleo vazio │
│ 3-8     │ CPU Core                   │ CPU funciona │
│ 6-10    │ Memória + Loader           │ ROM carrega  │
│ 9-14    │ BREW HLE                   │ APIs básicas │
│ 13-18   │ Gráficos + Áudio           │ Tela + Som   │
│ 17-22   │ Primeiro Jogo              │ 1 jogo       │
│ 20-28   │ Os 3 Jogos                 │ 3 jogos      │
│ 29+     │ Expansão                   │ 50+ jogos    │
└─────────┴────────────────────────────┴──────────────┘

Tempo total até 3 jogos: ~6-7 meses
Tempo total até 50 jogos: ~12+ meses
```

---

## 🎯 Milestones Principais

```
MILESTONE 1 (Mês 1):
"Núcleo carrega no RetroArch"

MILESTONE 2 (Mês 2):
"CPU executa código ARM"

MILESTONE 3 (Mês 3):
"ROM carrega e começa a executar"

MILESTONE 4 (Mês 4):
"Tela mostra imagem"

MILESTONE 5 (Mês 5):
"Primeiro jogo jogável"

MILESTONE 6 (Mês 6-7):
"3 jogos jogáveis (paridade Infuse)"

MILESTONE 7 (Mês 12+):
"50+ jogos (máxima compatibilidade)"
```

---

## 🔄 Metodologia de Trabalho

### Ciclo Iterativo

```
Para cada funcionalidade:

1. IMPLEMENTAR
   └─ Escrever o código

2. COMPILAR
   └─ Sem erros

3. TESTAR
   └─ Funciona isolado?

4. INTEGRAR
   └─ Funciona no todo?

5. DEBUGAR
   └─ Corrigir problemas

6. DOCUMENTAR
   └─ Anotar o que aprendeu

7. PRÓXIMO
   └─ Repetir
```

### Regra de Ouro

```
"Sempre tenha algo que compila e roda"

├─ Nunca deixe código quebrado por dias
├─ Commits pequenos e frequentes
├─ Cada commit = 1 funcionalidade ou 1 fix
└─ Se quebrar, você sabe o que foi
```

---

## 📈 Como Medir Progresso

```
Métricas para acompanhar:

CPU:
├─ Instruções implementadas: X / ~150
└─ Instruções testadas: X

APIs:
├─ APIs BREW implementadas: X
└─ APIs faltando (por jogo): X

Jogos:
├─ Jogos que carregam: X
├─ Jogos que mostram tela: X
├─ Jogos jogáveis: X
└─ Jogos perfeitos: X

Atualize semanalmente em PROGRESS.md
```

---

## 🎯 Próximo Passo

Agora você tem o plano completo. Próximos documentos detalham CADA parte:

→ **04_EMULACAO_CPU_DETALHADA.md** - Como emular a CPU
→ **05_EMULACAO_APIS_BREW.md** - Como emular as APIs
→ **06_TOOLCHAIN_SETUP.md** - Ferramentas necessárias

Continue lendo para os detalhes técnicos de cada fase!
