# 📁 Estrutura de Pasta - Organização do Projeto

> Como organizar os arquivos de forma profissional

---

## 📌 Por Que Organização Importa

```
Um núcleo tem dezenas de arquivos.
Sem organização, vira caos.

Boa estrutura:
├─ Fácil de encontrar código
├─ Fácil de adicionar features
├─ Fácil para outros entenderem
└─ Fácil de manter
```

---

## 🗂️ Estrutura Recomendada

```
zeebo_libretro/
│
├── src/                        # Código-fonte
│   │
│   ├── core/                   # Interface LibRetro
│   │   ├── libretro_core.c    # Funções retro_*
│   │   └── libretro_core.h
│   │
│   ├── cpu/                     # Emulador ARM
│   │   ├── cpu.c              # Estrutura e loop principal
│   │   ├── cpu.h
│   │   ├── decode.c          # Decodificação
│   │   ├── decode.h
│   │   ├── execute_arm.c     # Executor ARM
│   │   ├── execute_thumb.c   # Executor Thumb
│   │   └── flags.c           # Flags e condições
│   │
│   ├── memory/                 # Gerenciamento de memória
│   │   ├── memory.c
│   │   ├── memory.h
│   │   └── heap.c            # Alocador (MALLOC/FREE)
│   │
│   ├── loader/                 # Carregar ROMs
│   │   ├── mod_loader.c      # Arquivos MOD
│   │   ├── mod_loader.h
│   │   ├── mif_parser.c      # Metadados MIF
│   │   └── bar_parser.c      # Recursos BAR
│   │
│   ├── brew/                   # APIs BREW (HLE)
│   │   ├── brew.c            # Sistema central + trap
│   │   ├── brew.h
│   │   ├── ishell.c         # IShell
│   │   ├── idisplay.c       # IDisplay/IGraphics
│   │   ├── ibitmap.c        # IBitmap
│   │   ├── isound.c         # ISound
│   │   ├── ifile.c          # IFile
│   │   └── imemory.c        # MALLOC/FREE
│   │
│   ├── gpu/                    # Renderização
│   │   ├── framebuffer.c
│   │   ├── framebuffer.h
│   │   └── draw.c           # Funções de desenho
│   │
│   ├── audio/                  # Áudio
│   │   ├── audio.c
│   │   ├── audio.h
│   │   ├── pcm.c            # PCM playback
│   │   └── mixer.c         # Mixagem
│   │
│   ├── input/                  # Controle
│   │   ├── input.c
│   │   └── input.h
│   │
│   └── debug/                  # Ferramentas de debug
│       ├── log.c            # Logging
│       ├── log.h
│       ├── disasm.c        # Disassembler
│       └── trace.c         # Trace de execução
│
├── include/                    # Headers externos
│   └── libretro.h            # API LibRetro
│
├── tests/                      # Testes
│   ├── test_cpu.c           # Testes da CPU
│   ├── test_memory.c        # Testes de memória
│   └── roms/                # ROMs de teste
│
├── docs/                       # Documentação
│   ├── PROGRESS.md          # Progresso semanal
│   ├── NOTES.md            # Anotações técnicas
│   ├── ARM_NOTES.md        # Descobertas sobre ARM
│   └── BREW_NOTES.md       # Descobertas sobre BREW
│
├── build/                      # Arquivos compilados (gerado)
│   └── (criado pelo make)
│
├── Makefile                    # Build system
├── CMakeLists.txt             # Build alternativo
├── README.md                  # Descrição do projeto
├── LICENSE                    # Licença
└── .gitignore                 # Arquivos a ignorar no git
```

---

## 📂 Explicação de Cada Pasta

### src/core/
```
A interface com o RetroArch.
Aqui ficam as funções retro_* (retro_init, retro_run, etc).
Este é o "ponto de entrada" do núcleo.
```

### src/cpu/
```
O coração: emulador ARM.
├─ cpu.c: estrutura e loop fetch-decode-execute
├─ decode.c: transformar bytes em instruções
├─ execute_arm.c: executar instruções ARM
├─ execute_thumb.c: executar instruções Thumb
└─ flags.c: gerenciar flags e condições
```

### src/memory/
```
Gerenciamento de memória emulada.
├─ memory.c: read/write básico
└─ heap.c: alocador para MALLOC/FREE do jogo
```

### src/loader/
```
Carregar jogos.
├─ mod_loader.c: parse do executável MOD
├─ mif_parser.c: metadados (nome, ícone)
└─ bar_parser.c: recursos (imagens, sons)
```

### src/brew/
```
As APIs BREW (o "sistema operacional").
Cada arquivo = uma interface.
Aqui está a maior parte do trabalho de compatibilidade.
```

### src/gpu/
```
Renderização gráfica.
├─ framebuffer.c: o buffer de pixels
└─ draw.c: funções de desenho
```

### src/audio/
```
Sistema de áudio.
├─ pcm.c: reprodução PCM
└─ mixer.c: misturar múltiplos sons
```

### src/input/
```
Mapeamento de controle.
Traduz botões do RetroArch para eventos do jogo.
```

### src/debug/
```
Ferramentas para debugar.
├─ log.c: sistema de logging
├─ disasm.c: mostrar instruções legíveis
└─ trace.c: rastrear execução
```

---

## 📄 Convenção de Arquivos

### Header (.h) + Implementation (.c)

```
Cada componente tem 2 arquivos:

cpu.h  → declarações (o que existe)
cpu.c  → implementação (como funciona)

Header:
├─ Structs
├─ Declarações de função
└─ Constantes

Implementation:
├─ Corpo das funções
└─ Lógica interna
```

### Exemplo de Header

```
// cpu.h (conceitual)

#ifndef CPU_H
#define CPU_H

// Estrutura
typedef struct {
    ...
} CPU;

// Funções (só declaração)
void cpu_init(CPU *cpu);
void cpu_step(CPU *cpu);

#endif
```

### Exemplo de Implementation

```
// cpu.c (conceitual)

#include "cpu.h"

// Implementação
void cpu_init(CPU *cpu) {
    // código real aqui
}

void cpu_step(CPU *cpu) {
    // código real aqui
}
```

---

## 🎯 Princípios de Organização

### 1. Separação de Responsabilidades

```
Cada arquivo faz UMA coisa:
├─ cpu.c → só CPU
├─ memory.c → só memória
└─ audio.c → só áudio

NÃO misturar tudo em um arquivo gigante.
```

### 2. Baixo Acoplamento

```
Componentes independentes quando possível:
├─ CPU não precisa saber de áudio
├─ Áudio não precisa saber de gráficos
└─ Comunicação via interfaces claras
```

### 3. Nomes Claros

```
Bom:
├─ cpu_execute_instruction()
├─ memory_read32()
└─ brew_ishell_create_instance()

Ruim:
├─ do_stuff()
├─ func1()
└─ x()
```

---

## 📊 Organização por Fase

### Fase 0-1 (Início)

```
Você começa simples:

src/
├── core/libretro_core.c    # Esqueleto
├── cpu/cpu.c               # CPU básico
├── memory/memory.c         # Memória
└── debug/log.c            # Logging

Poucos arquivos, foco no essencial.
```

### Fase 3-4 (Crescimento)

```
Adiciona conforme precisa:

src/
├── core/
├── cpu/ (mais arquivos)
├── memory/
├── loader/ (novo)
├── brew/ (novo)
├── gpu/ (novo)
└── debug/
```

### Fase 7+ (Maduro)

```
Estrutura completa, muitos arquivos.
Bem organizado desde o início = fácil de crescer.
```

---

## 📝 Arquivos de Documentação Interna

### PROGRESS.md

```
Atualizado semanalmente.

# Progresso

## Semana X
### Feito
- Implementei instrução MOV
- Corrigi bug no loader

### Fazendo
- Implementando LDR

### Problemas
- STR não funciona com offset

## Estatísticas
- Instruções: 15/150
- Jogos rodando: 0
```

### NOTES.md

```
Anotações técnicas conforme descobre.

# Notas Técnicas

## ARM
- PC aponta 8 bytes à frente (pipeline)
- Flags só atualizam com bit S

## BREW
- IShell_CreateInstance usa class IDs
- IDisplay_Update deve ser chamado para mostrar
```

### ARM_NOTES.md / BREW_NOTES.md

```
Documentação específica que você descobre.
Salvar TUDO que aprende.
Vira sua própria referência.
```

---

## 🗃️ .gitignore

### O Que Ignorar no Git

```
# Arquivos compilados
build/
*.o
*.so
*.dll

# ROMs (não distribuir jogos)
tests/roms/*.mod

# Arquivos temporários
*.tmp
*.log

# Editor
.vscode/
.idea/
```

---

## 🎯 Padrão de Nomenclatura

### Arquivos

```
minusculo_com_underscore.c
minusculo_com_underscore.h

Exemplos:
├─ cpu.c
├─ mod_loader.c
├─ brew_ishell.c
```

### Funções

```
componente_acao()

Exemplos:
├─ cpu_init()
├─ memory_read32()
├─ brew_ishell_create()
```

### Constantes

```
MAIUSCULO_COM_UNDERSCORE

Exemplos:
├─ MEMORY_SIZE
├─ AEECLSID_DISPLAY
├─ MAX_INSTRUCTIONS
```

### Structs

```
CamelCase ou minusculo_t

Exemplos:
├─ ARM_CPU
├─ cpu_t
├─ rom_info_t
```

---

## 🎯 Checklist de Estrutura

```
Inicial:
[ ] Criar pasta do projeto
[ ] Criar subpastas (src, include, docs, etc)
[ ] Criar Makefile
[ ] Criar README.md
[ ] Criar .gitignore
[ ] git init

Organização:
[ ] Separar por componente (cpu, memory, etc)
[ ] Header + Implementation para cada
[ ] Nomes claros e consistentes

Documentação:
[ ] PROGRESS.md
[ ] NOTES.md
[ ] Comentários no código
```

---

## 🎯 Próximo Passo

Projeto organizado! Próximo:

→ **08_TESTES_ESTRATEGIA.md** - Como testar e quais jogos começar

Você tem estrutura. Agora precisa saber como testar sistematicamente.
