# 📖 Glossário Técnico - Referência Rápida

> Dicionário de todos os termos técnicos usados nos documentos do projeto. Consulte sempre que encontrar uma sigla ou palavra desconhecida.

---

## 📌 Como Usar

```
Este documento é uma REFERÊNCIA, não uma leitura sequencial.
Use Ctrl+F (ou busca do seu editor) para achar o termo que precisa.

Organizado por categoria:
├─ LibRetro / RetroArch
├─ Hardware / CPU (ARM)
├─ Zeebo / BREW
├─ Ferramentas de Desenvolvimento
└─ Conceitos Gerais de Emulação
```

---

## 🎮 LibRetro / RetroArch

```
LibRetro
└─ Interface padronizada que permite qualquer emulador ("núcleo")
   rodar dentro do RetroArch. É a "linguagem comum" entre seu
   código e o programa que mostra a tela.

RetroArch
└─ O programa (frontend) que carrega núcleos LibRetro e mostra
   os jogos na tela, com menu, configurações, save states, etc.

Núcleo (Core)
└─ Seu emulador compilado como biblioteca (.so/.dll/.dylib) que
   o RetroArch carrega e executa.

Callback
└─ Uma função que você "empresta" para outra parte do código chamar.
   Ex: RetroArch te dá video_cb, e você chama video_cb() quando
   quiser mostrar uma imagem.

retro_run()
└─ Função principal do núcleo, chamada ~60x por segundo. É onde
   você executa 1 frame do jogo (CPU + gráficos + áudio).

Framebuffer
└─ Um bloco de memória que representa a imagem da tela, pixel por
   pixel. Você desenha nele, depois envia para o RetroArch mostrar.

FPS (Frames Per Second)
└─ Quantas vezes por segundo a tela é atualizada. Zeebo usa 60 FPS.
```

---

## 🧠 Hardware / CPU (ARM)

```
ARM
└─ Uma família de arquiteturas de processador, usada em celulares,
   consoles portáteis e no Zeebo. É "RISC" (ver abaixo).

RISC (Reduced Instruction Set Computer)
└─ Filosofia de design de CPU com instruções simples e de tamanho
   fixo, ao contrário de CISC (x86) que tem instruções complexas.

Cortex-A8 / ARM11
└─ Modelos específicos de núcleo ARM. O Zeebo real usa ARM11
   (ARMv6); alguns emuladores usam Cortex-A8 (ARMv7) como
   referência para o comportamento HLE.

Registrador
└─ Um "espaço de armazenamento" dentro da CPU, muito rápido.
   ARM tem 16 registradores (R0-R15).

PC (Program Counter)
└─ Registrador especial (R15) que aponta para a próxima
   instrução a ser executada.

SP (Stack Pointer)
└─ Registrador especial (R13) que aponta para o topo da pilha
   (stack) atual.

LR (Link Register)
└─ Registrador especial (R14) que guarda o endereço de retorno
   quando uma função é chamada.

CPSR (Current Program Status Register)
└─ Registrador que guarda "flags" (N, Z, C, V) e o modo atual
   da CPU (User, IRQ, etc).

Flags (N, Z, C, V)
└─ Bits que indicam o resultado da última operação:
   N = negativo, Z = zero, C = carry (vai-um), V = overflow.

Opcode
└─ O valor numérico (em binário/hex) que identifica qual
   instrução deve ser executada.

Thumb
└─ Modo alternativo do ARM que usa instruções de 16 bits
   (em vez de 32 bits), economizando espaço.

Fetch-Decode-Execute
└─ O ciclo básico de qualquer CPU (real ou emulada):
   1) ler a instrução, 2) entender o que ela faz, 3) executá-la.

Little-endian
└─ Forma de guardar números na memória onde o byte menos
   significativo vem primeiro. ARM (e Zeebo) usam isso.

Branch
└─ Uma instrução que "pula" para outro lugar do código
   (equivalente a goto ou chamada de função).

Interpretador (CPU)
└─ Tipo de emulador que lê e executa uma instrução por vez,
   sem traduzir para código nativo. Mais simples, mais lento.

JIT (Just-In-Time Compilation)
└─ Técnica avançada onde blocos de código são traduzidos para
   instruções nativas do computador hospedeiro, executando
   muito mais rápido. Deixado para fases avançadas do projeto.
```

---

## 🕹️ Zeebo / BREW

```
Zeebo
└─ Console digital-only lançado pela TecToy em 2009 no Brasil
   e México, baseado em hardware de celular.

BREW (Binary Runtime Environment for Wireless)
└─ Plataforma da Qualcomm que roda no Zeebo, fornecendo as
   APIs que os jogos usam (parecido com um sistema operacional
   simplificado para aplicativos).

MOD (.mod)
└─ Arquivo executável do jogo em formato BREW - contém o
   código ARM e os dados que a CPU vai executar.

MIF (.mif / Module Information File)
└─ Arquivo de metadados do jogo: nome, ícone, IDs de classe,
   extensões necessárias. Usado para exibir no menu.

BAR (.bar / BREW Archive)
└─ Arquivo de recursos do jogo: imagens, sons, strings, layouts.

HLE (High-Level Emulation)
└─ Técnica de emulação onde você REIMPLEMENTA o comportamento
   das APIs do sistema (BREW), em vez de emular o sistema
   original bit a bit. É o caminho escolhido para este projeto.

LLE (Low-Level Emulation)
└─ Técnica onde você emula o sistema original de verdade,
   instrução por instrução (precisaria da ROM real do BREW,
   que não está disponível/não é viável).

Interface (no contexto BREW)
└─ Um "conjunto de funcionalidades" que o jogo pode pedir ao
   sistema, como IDisplay (tela), ISound (áudio), IFile
   (arquivos). Funciona de forma parecida com COM (Microsoft).

vtable (Virtual Table)
└─ Uma tabela de ponteiros para funções, usada para implementar
   interfaces. Quando o jogo chama um método, ele na verdade
   segue um ponteiro dentro dessa tabela.

Class ID
└─ Um número único que identifica qual interface BREW um jogo
   está pedindo (ex: AEECLSID_DISPLAY).

Trap (mecanismo de)
└─ Técnica usada pelo emulador para "interceptar" quando o jogo
   tenta chamar uma API BREW, redirecionando para o código HLE
   que você escreveu.

Applet
└─ O "aplicativo" BREW em execução (o próprio jogo, do ponto de
   vista do sistema operacional).

AEEMod_Load
└─ Função de entrada padrão que o BREW chama para carregar um
   módulo (jogo).
```

---

## 🔧 Ferramentas de Desenvolvimento

```
GCC / Clang
└─ Compiladores de C. Transformam seu código-fonte (.c) em
   código binário executável ou biblioteca.

Make / Makefile
└─ Ferramenta e arquivo de configuração que automatizam o
   processo de compilação de múltiplos arquivos.

CMake
└─ Ferramenta que gera Makefiles automaticamente, funcionando
   em múltiplos sistemas operacionais (Windows/Linux/Mac).

GDB (GNU Debugger)
└─ Ferramenta para inspecionar um programa enquanto ele roda:
   pausar, ver variáveis, executar linha por linha.

Ghidra
└─ Ferramenta de engenharia reversa (da NSA, gratuita) usada
   para analisar arquivos binários, como os MOD dos jogos.

Git
└─ Sistema de controle de versão, usado para salvar o histórico
   do código e permitir voltar atrás se algo quebrar.

.so / .dll / .dylib
└─ Extensões de biblioteca compartilhada em Linux (.so),
   Windows (.dll) e Mac (.dylib). É o formato final do núcleo.

-fPIC (flag de compilação)
└─ "Position Independent Code" - necessário para gerar
   bibliotecas compartilhadas (.so).

QEMU
└─ Emulador de CPU genérico, usado neste projeto como
   referência de baixo nível para comparar comportamento de
   instruções ARM.
```

---

## 🧩 Conceitos Gerais de Emulação

```
Emulação
└─ Simular o comportamento de um hardware/sistema diferente do
   seu, através de software.

Heap
└─ Região de memória usada para alocação dinâmica
   (MALLOC/FREE). Cresce conforme o programa pede memória.

Stack (Pilha)
└─ Região de memória usada para chamadas de função e variáveis
   locais. No ARM, cresce "para baixo" (de endereços altos para
   baixos).

Bounds Checking
└─ Verificação para garantir que um acesso de memória está
   dentro dos limites válidos, evitando crashes silenciosos.

Alinhamento (Alignment)
└─ Regra de que certos acessos de memória (16-bit, 32-bit)
   devem começar em endereços múltiplos de 2 ou 4.

Framebuffer / VRAM
└─ Memória dedicada a guardar a imagem que será mostrada na
   tela.

Sample Rate (áudio)
└─ Quantas amostras de áudio são geradas por segundo (Zeebo
   usa 44100 Hz, o padrão de CD).

PCM (Pulse Code Modulation)
└─ Formato de áudio "cru", sem compressão - o mais simples de
   emular.

Regressão (teste de)
└─ Verificar que uma mudança nova no código não quebrou algo
   que já funcionava antes.

Checkpoint / Milestone
└─ Um marco de progresso concreto e verificável (ex: "núcleo
   compila", "3 jogos jogáveis").
```

---

## 🎯 Abreviações Rápidas

```
API   - Application Programming Interface (interface de programação)
ARM   - Advanced RISC Machine (arquitetura de CPU)
BREW  - Binary Runtime Environment for Wireless
CPU   - Central Processing Unit (processador)
FPS   - Frames Per Second
GPU   - Graphics Processing Unit
HLE   - High-Level Emulation
LLE   - Low-Level Emulation
LR    - Link Register
PC    - Program Counter
RISC  - Reduced Instruction Set Computer
SP    - Stack Pointer
SWI   - Software Interrupt
VRAM  - Video RAM
```

---

## 🎯 Próximo Passo

Use este glossário como referência sempre que um termo técnico aparecer
em qualquer outro documento do projeto. Não é necessário ler tudo de
uma vez - volte aqui quando precisar.

→ Para continuar o planejamento prático, veja `12_CHECKLIST_SEMANA1.md`
  e `11_PROTOTIPO_SKELETON.md`.
