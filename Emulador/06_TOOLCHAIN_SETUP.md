# 🔧 Toolchain Setup - Ferramentas de Desenvolvimento

> Tudo que você precisa instalar e configurar para desenvolver o núcleo

---

## 📌 Visão Geral das Ferramentas

```
CATEGORIA          FERRAMENTA           PARA QUÊ
─────────────────────────────────────────────────────────
Compilador         GCC ou Clang         Compilar C
Build System       Make ou CMake        Automatizar build
API Headers        libretro.h           Interface LibRetro
Emulador Host      RetroArch            Testar o núcleo
Debugger           GDB                  Debugar código
Reverse Eng.       Ghidra               Analisar ROMs
Editor             VSCode (ou outro)    Escrever código
Version Control    Git                  Controlar versões
```

---

## 💻 Por Sistema Operacional

### Windows

```
OPÇÃO A: MSYS2/MinGW (recomendado para C nativo)
├─ Baixar MSYS2: https://www.msys2.org/
├─ Instalar
└─ No terminal MSYS2:
   pacman -S mingw-w64-x86_64-gcc
   pacman -S mingw-w64-x86_64-make
   pacman -S mingw-w64-x86_64-cmake
   pacman -S mingw-w64-x86_64-gdb
   pacman -S git

OPÇÃO B: WSL2 (Linux dentro do Windows)
├─ Ativar WSL2
├─ Instalar Ubuntu
└─ Seguir instruções de Linux abaixo

OPÇÃO C: Visual Studio
├─ Baixar VS Community
├─ Workload "Desktop C++"
└─ Usar CMake integrado
```

### Linux (Ubuntu/Debian)

```
sudo apt update
sudo apt install build-essential   # gcc, make
sudo apt install cmake
sudo apt install gdb
sudo apt install git

# Opcional (para gráficos/áudio depois)
sudo apt install libsdl2-dev
sudo apt install libgl1-mesa-dev

# RetroArch para testar
sudo apt install retroarch
```

### macOS

```
# Instalar Homebrew primeiro (brew.sh)

brew install gcc
brew install cmake
brew install gdb
brew install git

# RetroArch
brew install --cask retroarch
```

---

## 📦 Ferramenta 1: Compilador (GCC/Clang)

### O Que É

```
Transforma seu código C em executável/biblioteca.

Você escreve:  arquivo.c
Compilador faz: arquivo.o → biblioteca.dll/.so
```

### Verificar Instalação

```
gcc --version

Deve mostrar algo como:
gcc (Ubuntu 11.4.0) 11.4.0
```

### Comandos Básicos

```
Compilar um arquivo:
gcc -c arquivo.c -o arquivo.o

Criar biblioteca compartilhada:
gcc -shared *.o -o zeebo_libretro.so

Com warnings (recomendado):
gcc -Wall -Wextra -c arquivo.c
```

---

## 📦 Ferramenta 2: Build System (Make/CMake)

### Por Que Precisa

```
Um núcleo tem MUITOS arquivos.
Compilar cada um manualmente é inviável.
Build system automatiza tudo.

Você digita: make
E ele compila TODOS os arquivos corretamente.
```

### Make (Mais Simples)

```
Você cria um "Makefile" que descreve como compilar.

Vantagens:
├─ Simples
├─ Padrão em Linux
└─ Fácil de entender

Uso:
make          # compilar
make clean    # limpar
```

### CMake (Mais Poderoso)

```
Gera Makefiles automaticamente. Multiplataforma.

Vantagens:
├─ Funciona em Windows/Linux/Mac
├─ Mais flexível
└─ Padrão em projetos grandes

Uso:
mkdir build && cd build
cmake ..
make
```

### Recomendação

```
Para começar: Make (mais simples)
Se precisar multiplataforma: CMake

Ambos funcionam. Comece com o que for mais confortável.
```

---

## 📦 Ferramenta 3: libretro.h

### O Que É

```
O arquivo header que define a interface LibRetro.
Contém todas as structs e funções que você precisa.

Sem ele, você não pode fazer um núcleo.
```

### Onde Conseguir

```
Do repositório oficial:
https://github.com/libretro/libretro-common

Você precisa de:
├─ libretro.h (principal)
└─ (outros headers de libretro-common conforme necessário)
```

### Como Usar

```
No seu código:
#include "libretro.h"

E você tem acesso a:
├─ struct retro_system_info
├─ struct retro_game_info
├─ retro_video_refresh_t
├─ RETRO_DEVICE_JOYPAD
└─ ... tudo que precisa
```

---

## 📦 Ferramenta 4: RetroArch

### Por Que Precisa

```
Para TESTAR seu núcleo, você precisa do RetroArch.

Fluxo:
1. Você compila seu núcleo (.dll/.so)
2. Coloca na pasta de cores do RetroArch
3. Abre RetroArch
4. Carrega seu núcleo
5. Testa um jogo
```

### Onde Colocar o Núcleo

```
Windows:
RetroArch\cores\zeebo_libretro.dll

Linux:
~/.config/retroarch/cores/zeebo_libretro.so

Mac:
~/Library/Application Support/RetroArch/cores/
```

### Como Testar

```
1. Abrir RetroArch
2. Load Core → escolher zeebo
3. Load Content → escolher jogo.mod
4. Ver se funciona
```

---

## 📦 Ferramenta 5: GDB (Debugger)

### O Que É

```
Permite inspecionar seu programa enquanto roda.

Você pode:
├─ Pausar em qualquer linha (breakpoint)
├─ Ver valores de variáveis
├─ Executar linha por linha
└─ Ver onde crashou
```

### Comandos Essenciais

```
gdb ./programa       # iniciar
(gdb) break main     # breakpoint em main
(gdb) run            # executar
(gdb) next           # próxima linha
(gdb) step           # entrar em função
(gdb) print var      # ver variável
(gdb) continue       # continuar
(gdb) backtrace      # ver call stack
(gdb) quit           # sair
```

### Debugar Núcleo (Especial)

```
Núcleos são bibliotecas, não executáveis.
Para debugar, você anexa ao RetroArch:

gdb retroarch
(gdb) break retro_run
(gdb) run -L zeebo_libretro.so jogo.mod
```

---

## 📦 Ferramenta 6: Ghidra (Reverse Engineering)

### O Que É

```
Ferramenta para analisar código binário.
Da NSA, gratuita e poderosa.

Você usa para:
├─ Analisar arquivos MOD (jogos)
├─ Ver quais APIs eles chamam
├─ Entender o formato
└─ Descobrir entry points
```

### Onde Conseguir

```
https://ghidra-sre.org/

Requer Java instalado.
```

### Uso Básico

```
1. Criar projeto
2. Importar arquivo MOD
3. Deixar analisar
4. Ver:
   ├─ Funções
   ├─ Strings
   ├─ Chamadas de API
   └─ Estrutura do código
```

### Alternativas

```
├─ IDA Free (limitado, mas bom)
├─ radare2 (command line, poderoso)
├─ objdump (simples, já vem com gcc)
```

---

## 📦 Ferramenta 7: Editor de Código

### Opções

```
VSCode (recomendado):
├─ Grátis
├─ Extensões C/C++
├─ Debugging integrado
└─ https://code.visualstudio.com/

Alternativas:
├─ CLion (pago, poderoso)
├─ Vim/Neovim (avançado)
├─ Sublime Text
└─ Qualquer editor de texto
```

### Extensões VSCode Úteis

```
├─ C/C++ (Microsoft)
├─ CMake Tools
├─ Makefile Tools
└─ GitLens
```

---

## 📦 Ferramenta 8: Git (Version Control)

### Por Que Precisa

```
Salvar histórico do seu código.

Vantagens:
├─ Voltar atrás se quebrar
├─ Ver o que mudou
├─ Backup (GitHub)
└─ Organizar desenvolvimento
```

### Comandos Essenciais

```
git init                 # iniciar repositório
git add arquivo.c        # adicionar arquivo
git commit -m "mensagem" # salvar
git log                  # ver histórico
git status               # ver mudanças
git checkout arquivo.c   # desfazer mudanças
```

### Fluxo Recomendado

```
Cada vez que implementa algo:
1. git add (arquivos mudados)
2. git commit -m "descrição clara"

Exemplo:
git commit -m "Implementar instrução MOV"
git commit -m "Adicionar IShell_CreateInstance"
git commit -m "Corrigir bug no framebuffer"
```

---

## 🎯 Setup Completo (Checklist)

```
Instalação:
[ ] Compilador (gcc/clang)
[ ] Build system (make/cmake)
[ ] Git
[ ] GDB
[ ] Editor (VSCode)
[ ] RetroArch
[ ] Ghidra (opcional agora)

Download:
[ ] libretro.h
[ ] libretro-common (headers)

Verificação:
[ ] gcc --version funciona
[ ] make --version funciona
[ ] git --version funciona
[ ] RetroArch abre
[ ] Consegue compilar "hello world" em C
```

---

## 🧪 Teste de Setup: Hello World

### Criar arquivo teste.c

```
Conteúdo:

#include <stdio.h>
int main() {
    printf("Setup funcionando!\n");
    return 0;
}
```

### Compilar e rodar

```
gcc teste.c -o teste
./teste

Deve imprimir: Setup funcionando!
```

### Se funcionou

```
✅ Compilador OK
✅ Ambiente pronto
✅ Pode começar o projeto
```

---

## 🏗️ Estrutura de Build (Conceitual)

### Como o Núcleo é Compilado

```
Passo 1: Compilar cada .c em .o
├─ cpu.c      → cpu.o
├─ memory.c   → memory.o
├─ loader.c   → loader.o
├─ brew.c     → brew.o
└─ core.c     → core.o

Passo 2: Linkar todos em uma biblioteca
├─ cpu.o + memory.o + ... → zeebo_libretro.so

Passo 3: Copiar para RetroArch
└─ zeebo_libretro.so → RetroArch/cores/
```

### Flags Importantes

```
-fPIC          : Position Independent Code (necessário p/ .so)
-shared        : Criar biblioteca compartilhada
-Wall -Wextra  : Mostrar warnings (pegar bugs cedo)
-g             : Símbolos de debug (para GDB)
-O2            : Otimização (para release)
```

---

## ⚠️ Problemas Comuns de Setup

```
"gcc não encontrado"
└─ Compilador não instalado ou não no PATH

"libretro.h não encontrado"
└─ Header não baixado ou path errado

"RetroArch não vê o núcleo"
└─ Núcleo na pasta errada ou não compilado como .so/.dll

"undefined reference"
└─ Faltou linkar algum arquivo .o

"cannot open shared library"
└─ Faltou flag -fPIC ou -shared
```

---

## 🎯 Próximo Passo

Ambiente pronto! Próximo documento:

→ **07_ESTRUTURA_PASTA.md** - Como organizar os arquivos do projeto

Você tem as ferramentas. Agora precisa saber como organizar o código.
