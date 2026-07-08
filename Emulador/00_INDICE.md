# 📋 ÍNDICE COMPLETO - Núcleo LibRetro Zeebo

> Planejamento detalhado de como criar um núcleo RetroArch que emula Zeebo

---

## 🎯 Visão Geral Executiva

```
OBJETIVO FINAL:
├─ Criar núcleo LibRetro para Zeebo
├─ Funcionar no RetroArch (Windows, Linux, Mac)
├─ Rodar 3 jogos inicialmente (like Infuse)
├─ Expandir para 50+ jogos (máximo compatível)
└─ Código profissional, bem estruturado

TIMELINE:
├─ Fase 1 (Planejamento): ✅ COMPLETA
├─ Fase 0 (Setup + Estrutura): próxima etapa, pronta para começar
├─ Fase 2 (Desenvolvimento CPU/BREW): 3-6 meses
├─ Fase 3 (Testes/Iteração): Contínuo
└─ Total estimado: 3-6 meses de desenvolvimento real

VOCÊ ESTÁ EM:
└─ FASE 1 - Planejamento: 100% COMPLETA (14 documentos)
   Pronta para iniciar a Fase 0 (skeleton compilando) quando você quiser.

NOTA IMPORTANTE SOBRE ESTE PROJETO:
├─ Lucas (o responsável pelo projeto) não é programador
├─ Todas as decisões técnicas (arquitetura, algoritmos, trade-offs)
│  são tomadas e implementadas pela IA, sem exigir conhecimento
│  técnico prévio do Lucas
└─ Os documentos já registram as decisões técnicas tomadas -
   não são mais perguntas em aberto, e sim escolhas fechadas
```

---

## 📚 Documentação Completa (14 documentos)

Todos os documentos abaixo já foram criados e estão na Fase 1: Planejamento.

### 1. **01_ARQUITETURA_LIBRETRO.md** ✅
   - O que é LibRetro
   - Como estruturar um núcleo
   - Interface entre núcleo e RetroArch
   - Callbacks e hooks

### 2. **02_ARQUITETURA_ZEEBO.md** ✅
   - Hardware Zeebo (o que emular)
   - CPU ARM Cortex-A8 / ARM11
   - Memória e mapa de endereços
   - Periféricos (gráficos, som, input)

### 3. **03_PLANO_DESENVOLVIMENTO.md** ✅
   - Fases de implementação
   - Milestones
   - Checkpoints de progresso
   - Timeline realista

### 4. **04_EMULACAO_CPU_DETALHADA.md** ✅ (atualizado)
   - Como emular CPU ARM
   - Instruction set (quais implementar)
   - Prioritização (qual primeiro)
   - Decisões de escopo fechadas: modo único (User), pipeline
     "fake" via PC+8/+4, tratamento de erros sem travar,
     barrel shifter e immediates rotacionados desde a Fase 1,
     tabela completa das 16 condições

### 5. **05_EMULACAO_APIS_BREW.md** ✅ (atualizado)
   - APIs BREW que precisam ser emuladas
   - HLE vs LLE
   - Quais APIs os 3 primeiros jogos usam
   - Decisão fechada: mecanismo de trap via endereços mágicos
     (não SWI)

### 6. **06_TOOLCHAIN_SETUP.md** ✅
   - Ferramentas necessárias
   - Setup no Windows/Linux/Mac
   - Build system (Makefile, CMake)
   - Compilar código C do zero

### 7. **07_ESTRUTURA_PASTA.md** ✅
   - Como organizar projeto
   - Quais arquivos/pastas
   - Padrão profissional
   - Documentação interna

### 8. **08_TESTES_ESTRATEGIA.md** ✅
   - Como testar o núcleo
   - Quais 3 jogos começar
   - Como debugar cada problema
   - Métrica de progresso

### 9. **09_MAPEAMENTO_MEMORIAS.md** ✅ (atualizado)
   - Endereços importantes
   - Mapa de memória completo
   - Stack, heap, código, dados
   - Decisões fechadas: bounds checking nunca trava (loga e
     retorna 0/ignora), acesso desalinhado é permitido (loga
     aviso, sem simular rotação de hardware)

### 10. **10_INSTRUCOES_ARM.md** ✅ (atualizado)
   - Lista de todas instruções ARM
   - Quais primeiras (fase 1-2)
   - Quais depois (fase 3+)
   - Nota sobre barrel shifter como parte do encoding
   - Fora de escopo: coprocessador e Thumb-2 (não usados pelo
     ARM11/Zeebo)

### 11. **11_MULTIPLATAFORMA_BUILD.md** ✅
   - Como portar o núcleo para Windows/Linux/Mac/Android/consoles
   - CMake multiplataforma
   - Buildbot do RetroArch

### 12. **11_PROTOTIPO_SKELETON.md** ✅
   - Guia passo a passo do primeiro código real
   - Skeleton completo que compila e aparece no RetroArch

### 13. **12_CHECKLIST_SEMANA1.md** ✅
   - Checklist prático de execução da Fase 0
   - O que fazer se travar em cada bloco

### 14. **13_GLOSSARIO_TECNICO.md** ✅
   - Dicionário de todos os termos técnicos usados no projeto

---

## 🗺️ Mapa Mental do Projeto

```
┌─────────────────────────────────────────────────────────┐
│          NÚCLEO LIBRETRO ZEEBO                          │
├─────────────────────────────────────────────────────────┤
│                                                           │
│  ┌──────────────┐         ┌──────────────┐              │
│  │  LIBRETRO    │         │   ZEEBO      │              │
│  │  Interface   │         │  Emulation   │              │
│  └──────────────┘         └──────────────┘              │
│       ↑                          ↑                        │
│       │                          │                        │
│  ┌────┴───────────────────────────┴────┐                │
│  │    Core Implementation (C)           │                │
│  │                                      │                │
│  ├─ CPU Emulator (ARM)                  │                │
│  │  ├─ Decoder de Instruções            │                │
│  │  ├─ Executor de Instruções           │                │
│  │  ├─ Flags e Modo                     │                │
│  │  └─ Ciclos de Clock                  │                │
│  │                                      │                │
│  ├─ Memory Manager                      │                │
│  │  ├─ Leitura/Escrita                  │                │
│  │  ├─ Alocação                         │                │
│  │  └─ Proteção                         │                │
│  │                                      │                │
│  ├─ BREW HLE Layer                      │                │
│  │  ├─ IShell                           │                │
│  │  ├─ IDisplay                         │                │
│  │  ├─ ISound                           │                │
│  │  ├─ IFile                            │                │
│  │  ├─ INet (stub)                      │                │
│  │  └─ APIs...                          │                │
│  │                                      │                │
│  ├─ ROM Loader (MOD/MIF)                │                │
│  │  ├─ Parse MOD header                 │                │
│  │  ├─ Carregar código                  │                │
│  │  └─ Carregar dados                   │                │
│  │                                      │                │
│  ├─ Graphics Engine                     │                │
│  │  ├─ Framebuffer                      │                │
│  │  ├─ Sprite Rendering                 │                │
│  │  └─ VRAM                             │                │
│  │                                      │                │
│  ├─ Audio Engine                        │                │
│  │  ├─ PCM Player                       │                │
│  │  ├─ MIDI Player                      │                │
│  │  └─ Audio Buffer                     │                │
│  │                                      │                │
│  ├─ Input Handler                       │                │
│  │  └─ Map Controle → Botões ARM        │                │
│  │                                      │                │
│  └─ Debug Tools                         │                │
│     ├─ Logging                          │                │
│     ├─ Breakpoints                      │                │
│     └─ Disassembler                     │                │
│                                         │                │
└─────────────────────────────────────────┘                │
                                                             │
┌──────────────────────────────────────────────────────────┐│
│ RetroArch (UI, Menu, Salvar Progresso, etc)             ││
└──────────────────────────────────────────────────────────┘│
                                                             │
└─────────────────────────────────────────────────────────┘
```

---

## 🎯 O Que Você Vai Aprender

Depois de ler toda documentação:

```
✅ Como LibRetro funciona (arquitetura)
✅ Como Zeebo funciona (hardware)
✅ Como emular CPU (teoria + prática)
✅ Como estruturar projeto grande (profissional)
✅ Como fazer HLE de APIs (técnica avançada)
✅ Como debugar emulador (ferramentas)
✅ Como otimizar performance (importante)
✅ Como testar sistematicamente (metodologia)
```

---

## 📈 Roadmap Alto Nível

```
SEMANA 1-2 (AGORA):
├─ Planejar arquitetura
├─ Desenhar estrutura
├─ Listar requisitos
└─ Definir timeline

MÊS 1 (Quando começar dev):
├─ Setup toolchain
├─ Estrutura básica
├─ CPU interpreter básico
└─ Teste com programa trivial

MÊS 2:
├─ Expandir instruction set
├─ Implementar Thumb mode
├─ Memory management
└─ Teste com mais programas

MÊS 3:
├─ BREW HLE básico
├─ IDisplay + ISound
├─ ROM loader funcional
└─ Primeiro jogo roda (talvez buggy)

MÊS 4-5:
├─ Debugar 3 jogos alvo
├─ Fixes específicos por jogo
├─ Performance optimization
└─ 3 jogos completamente jogáveis

MÊS 6+:
├─ Expandir para mais jogos
├─ Implementar mais APIs
├─ Aumentar compatibilidade
└─ Chegar em 50+ jogos

TOTAL: 3-6 meses de desenvolvimento intenso
```

---

## 🎮 Os 3 Primeiros Jogos Alvo

Você vai começar com estes (como Infuse faz):

```
1. CRASH BANDICOOT NITRO KART 3D
   ├─ Jogo 3D simples
   ├─ Usa gráficos básicos
   ├─ Bom para testar rendering
   └─ Já roda em Infuse

2. DOUBLE DRAGON
   ├─ Fighting game
   ├─ Sprites 2D
   ├─ Usa áudio
   ├─ Bom para testar som
   └─ Já roda em Infuse

3. ZEEBO FAMILY PACK
   ├─ Mini-game collection
   ├─ Usa muito input
   ├─ Simples graficamente
   └─ Já roda em Infuse
```

Depois expandir para:
```
├─ Asphalt Urban GT
├─ Resident Evil 4
├─ Quake
├─ Tetris
├─ Zenonia
└─ ... 40+ mais
```

---

## 🔧 Próximas Leituras

1. **ARQUITETURA_LIBRETRO.md** (comece por aqui)
   - Entender como conectar ao RetroArch
   
2. **ARQUITETURA_ZEEBO.md** (depois)
   - Entender o que está emulando
   
3. **PLANO_DESENVOLVIMENTO.md** (depois)
   - Timeline e fases
   
4. **Outros** (conforme necessário)
   - Detalhe técnico específico

---

## 💡 Metodologia de Planejamento

Vou detalhar TUDO:

```
Para cada componente:
├─ O QUE É (explicação)
├─ POR QUÊ (motivo/importância)
├─ COMO FUNCIONA (teoria)
├─ COMO IMPLEMENTAR (passo a passo)
├─ EXEMPLOS (código pseudocódigo)
├─ TESTES (como testar)
└─ CHECKLIST (marcos)

Sem deixar brecha para dúvida!
```

---

## 🎯 Sua Próxima Ação

A documentação de planejamento está 100% completa e todas as decisões
técnicas em aberto já foram fechadas pela IA (ver notas de "decisão de
escopo" em cada documento atualizado).

Próximo passo real, quando você quiser:
1. Seguir `12_CHECKLIST_SEMANA1.md` passo a passo
2. Isso vai gerar o skeleton descrito em `11_PROTOTIPO_SKELETON.md`
3. Ao final, o núcleo "Zeebo" aparece na lista do RetroArch (vazio,
   mas funcionando) - isso marca o fim da Fase 0
4. Depois disso, começa a Fase 1 de código real: CPU (`04_EMULACAO_CPU_DETALHADA.md`)

Você não precisa entender os detalhes técnicos dos documentos 04, 05,
09 e 10 para avançar - eles servem como referência para a IA durante
a implementação. Seu papel é decidir o rumo do projeto (quais jogos,
prioridades, quando avançar de fase); os detalhes de C, ARM e BREW
ficam por conta da IA.
