# 🌍 Compatibilidade Multiplataforma e Build System

> Como portar seu núcleo para Windows, Android, Xbox, PSP e todas as plataformas

---

## 📌 Objetivo

```
Seu núcleo deve funcionar em:
├─ Desktop (Windows, Linux, macOS)
├─ Mobile (Android, iOS)
├─ Consoles Modernos (Xbox, PlayStation, Nintendo Switch)
├─ Consoles Antigos (PSP, Dreamcast, Wii)
└─ Embedded (Raspberry Pi, etc)

UM ÚNICO CÓDIGO FONTE para tudo!
```

---

## 🎯 Princípios de Portabilidade

### 1. Zero Dependências de SO

```c
❌ NÃO FAÇA:
#include <windows.h>
#include <unistd.h>
#include <dirent.h>

✅ FAÇA:
#include <stdio.h>      // Standard C
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
```

### 2. Sem Paths Hardcoded

```c
❌ PROBLEMA:
const char *config_path = "C:\\Users\\You\\zeebo\\config.ini";

✅ SOLUÇÃO:
// Use RetroArch para obter paths
const char *system_dir = NULL;
environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIR, &system_dir);
// system_dir agora é ~/.config/retroarch/system em Linux
//                      ou AppData em Windows, etc
```

### 3. Sem Assembly Específico

```c
❌ PROBLEMA:
#ifdef _M_X64
    // x86_64 specific
    asm("mov rax, rbx");
#endif

✅ SOLUÇÃO:
uint64_t result = a + b;  // C puro funciona em tudo
```

### 4. Tipos de Dados Portáveis

```c
❌ PROBLEMA:
int valor = 0x12345678;          // tamanho varia

✅ SOLUÇÃO:
uint32_t valor = 0x12345678;     // sempre 32-bit
uint16_t outro = 0x1234;         // sempre 16-bit
uint8_t byte = 0xFF;             // sempre 8-bit
```

### 5. Endianness Explícito

```c
// Zeebo é little-endian
// Mas seu código precisa saber disso

uint32_t read32_le(const uint8_t *data) {
    return (data[0]      ) |
           (data[1] << 8 ) |
           (data[2] << 16) |
           (data[3] << 24);
}

// Funciona em QUALQUER plataforma
// ARM, x86, MIPS, PowerPC, etc
```

---

## 🏗️ Build System Multiplataforma

### Opção 1: Make (Simples, Mas Manual)

```makefile
# Makefile portável (conceitual)

CC ?= gcc
CFLAGS ?= -Wall -Wextra -fPIC -O2

SOURCES = src/cpu.c src/memory.c src/brew.c src/core.c
OBJECTS = $(SOURCES:.c=.o)
TARGET = zeebo_libretro.so

ifeq ($(OS),Windows_NT)
    TARGET = zeebo_libretro.dll
    LDFLAGS = -shared
else ifeq ($(UNAME_S),Linux)
    TARGET = zeebo_libretro.so
    LDFLAGS = -shared -fPIC
else ifeq ($(UNAME_S),Darwin)
    TARGET = zeebo_libretro.dylib
    LDFLAGS = -shared -fPIC
endif

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)
```

**Problema:** Precisa atualizar Makefile para cada plataforma.

### Opção 2: CMake (Recomendado)

```cmake
# CMakeLists.txt - portável automaticamente

cmake_minimum_required(VERSION 3.10)
project(zeebo_libretro C)

set(SOURCES
    src/cpu.c
    src/memory.c
    src/brew.c
    src/core.c
    # ... mais arquivos
)

# Biblioteca compartilhada
add_library(zeebo_libretro SHARED ${SOURCES})

# Flags por plataforma (automático!)
if(MSVC)
    target_compile_options(zeebo_libretro PRIVATE /W4)
else()
    target_compile_options(zeebo_libretro PRIVATE -Wall -Wextra -fPIC)
endif()

# Saída em lugar certo
set_target_properties(zeebo_libretro PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
```

**Uso:**
```bash
# Windows
mkdir build
cd build
cmake .. -G "Visual Studio 16 2019"
cmake --build .

# Linux
mkdir build
cd build
cmake ..
make

# macOS
mkdir build
cd build
cmake ..
make
```

**CMake faz tudo automaticamente!**

### Recomendação

```
COMECE: Make (simples para começar)
DEPOIS: CMake (quando crescer)

Muitos projetos libretro já usam CMake.
```

---

## 🖥️ Compilando para Desktop

### Windows

#### Opção A: MinGW (Recomendado para C nativo)

```bash
# Instalar MSYS2/MinGW

# No terminal MSYS2:
pacman -S mingw-w64-x86_64-gcc
pacman -S mingw-w64-x86_64-cmake

# Compilar
mkdir build
cd build
cmake .. -G "Unix Makefiles"
make

# Resultado
# zeebo_libretro.dll em build/lib/
```

#### Opção B: Visual Studio

```bash
# Abrir cmd
mkdir build
cd build

# Gerar projeto VS
cmake .. -G "Visual Studio 16 2019" -A x64

# Compilar
cmake --build . --config Release

# Resultado
# zeebo_libretro.dll em build/lib/Release/
```

### Linux

```bash
# Ubuntu/Debian
sudo apt install build-essential cmake

mkdir build
cd build
cmake ..
make

# Resultado
# zeebo_libretro.so em build/lib/
```

### macOS

```bash
# Homebrew
brew install cmake

mkdir build
cd build
cmake ..
make

# Resultado: zeebo_libretro.dylib
# (Intel x86_64 ou Apple Silicon ARM64, automático)
```

---

## 📱 Compilando para Android

### Processo Geral

```
Android requer Android NDK (Native Development Kit)

1. Instalar NDK
2. Configurar CMakeLists.txt para Android
3. Compilar para arquitetura específica
4. Testar em device/emulador
```

### Passo 1: Instalar NDK

```bash
# Opção A: Android Studio (interface gráfica)
# Opção B: Download manual
wget https://dl.google.com/android/repository/android-ndk-r21e-linux-x86_64.zip
unzip android-ndk-r21e-linux-x86_64.zip
export NDK_PATH=/path/to/android-ndk-r21e
```

### Passo 2: CMakeLists.txt para Android

```cmake
# CMakeLists.txt (com suporte Android)

cmake_minimum_required(VERSION 3.10)
project(zeebo_libretro C)

# Se compilando para Android
if(ANDROID)
    set(CMAKE_C_COMPILER ${ANDROID_TOOLCHAIN}/bin/clang)
    set(CMAKE_CXX_COMPILER ${ANDROID_TOOLCHAIN}/bin/clang++)
endif()

add_library(zeebo_libretro SHARED ${SOURCES})

# ... resto igual
```

### Passo 3: Compilar

```bash
# Para Android ARM64 (mais comum)
mkdir build_android
cd build_android

cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=$NDK_PATH/build/cmake/android.toolchain.cmake \
    -DANDROID_PLATFORM=android-21 \
    -DANDROID_ABI=arm64-v8a

make

# Resultado: zeebo_libretro.so (ARM64)
```

### Arquiteturas Android

```
arm64-v8a       ✅ Mais comum (64-bit ARM)
armeabi-v7a     ✅ Compatibilidade (32-bit ARM)
x86             ⚠️  Alguns devices
x86_64          ⚠️  Alguns devices

Compilar para arm64-v8a cobre 95% dos devices modernos.
```

### Testar em Android

```bash
# Copiar .so para device
adb push zeebo_libretro.so /data/data/com.retroarch/

# RetroArch no Android vai encontrar automaticamente

# Ou via interface gráfica
# RetroArch > Load Core > zeebo
```

---

## 🎮 Compilando para Consoles

### Xbox One/Series X

```
Requer:
├─ Xbox One/Series X Development Kit
├─ XDK (Xbox Development Kit)
└─ Licença de desenvolvedor Microsoft

CMakeLists.txt precisa de:
├─ Compilador MSVC com toolchain Xbox
├─ Flags e libraries específicas

Processo:
1. Abrir XDK
2. Criar novo projeto
3. Adicionar seus arquivos .c
4. Compilar
5. Deploy para console
```

**Nota:** Requer acesso a ferramentas Microsoft propriedárias.

### PlayStation 4/5

```
Requer:
├─ PS4/PS5 Development Kit
├─ SDK Sony
└─ Licença Sony

Semelhante ao Xbox:
1. Instalar SDK
2. Configurar CMake
3. Compilar
4. Deploy

Nota: Muito mais restritivo que Xbox.
```

### Nintendo Switch

```
Requer:
├─ Switch Development Kit (DevKit Switch)
├─ SDK Nintendo
└─ Licença Nintendo

RetroArch para Switch é mais complexo.
Mas o processo é similar.
```

---

## 🕹️ Compilando para Consoles Antigos

### PSP (PlayStation Portable)

```
Requer:
├─ PSP Toolchain (psp-gcc, psp-binutils)
├─ SDK PSPSDK
└─ Emulador PSP (PPSSPP para testar)

Setup (Linux/WSL):
1. Clonar pspdev repo: github.com/pspsdk/pspsdk
2. Compilar toolchain
3. Configurar CMake

Compilar:
mkdir build_psp
cd build_psp
cmake .. -DCMAKE_TOOLCHAIN_FILE=psp-toolchain.cmake
make

Resultado: zeebo_libretro.prx (executável PSP)
```

### Dreamcast

```
Requer:
├─ Dreamcast Toolchain
├─ KallistiOS (OS kernel)
└─ Emulador (Flycast)

Similar ao PSP.
Menos usado mas possível.
```

### Wii

```
Requer:
├─ libogc (Wii development library)
├─ devkitPRO
└─ Emulador (Dolphin)

Especifico para Wii.
PowerPC architecture (diferente de ARM).
```

---

## 🍓 Compilando para Raspberry Pi

```
Raspberry Pi roda Linux (Raspbian).

Setup:
1. SSH em Pi
2. sudo apt install build-essential cmake
3. git clone seu_repo
4. mkdir build && cd build
5. cmake ..
6. make

Resultado: zeebo_libretro.so ARM (ARM32 ou ARM64, automático)

RetroArch no Pi encontra automaticamente.
```

---

## 📊 Matriz de Compilação

```
┌──────────────────┬────────────┬──────────────┬──────────────┐
│ Plataforma       │ Arquitetura│ Compilador   │ Output       │
├──────────────────┼────────────┼──────────────┼──────────────┤
│ Windows          │ x86_64     │ MSVC/MinGW   │ .dll         │
│ Linux x86        │ x86_64     │ gcc/clang    │ .so          │
│ macOS Intel      │ x86_64     │ clang        │ .dylib       │
│ macOS Apple Si   │ arm64      │ clang        │ .dylib       │
│ Android          │ arm64-v8a  │ clang (NDK)  │ .so          │
│ Android          │ armeabi-v7a│ clang (NDK)  │ .so          │
│ iOS              │ arm64      │ clang        │ .dylib       │
│ Xbox One         │ x86_64     │ MSVC (XDK)   │ .xex         │
│ PlayStation 4    │ x86_64     │ MSVC (SDK)   │ .prx         │
│ Switch           │ arm64      │ gcc/clang    │ .nso         │
│ PSP              │ MIPS       │ mips-gcc     │ .prx         │
│ Dreamcast        │ SH-4       │ sh-elf-gcc   │ .elf         │
│ Wii              │ PowerPC    │ powerpc-gcc  │ .elf         │
│ Raspberry Pi     │ arm/arm64  │ gcc/clang    │ .so          │
└──────────────────┴────────────┴──────────────┴──────────────┘
```

---

## 🔧 Técnicas de Portabilidade

### Usar Preprocessor Directives Com Cuidado

```c
// Aceito: detectar plataforma para flags de compilação
#ifdef __ANDROID__
    #define PLATFORM_ANDROID 1
#elif _WIN32
    #define PLATFORM_WINDOWS 1
#else
    #define PLATFORM_UNIX 1
#endif

// Não aceito: código DIFERENTE por plataforma
// Manter lógica igual, só flags mudam
```

### Abstrair I/O Dependente de SO

```c
// Em vez de:
FILE *f = fopen("C:\\config.ini", "r");

// Fazer:
const char *system_dir = NULL;
environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIR, &system_dir);
char config_path[256];
snprintf(config_path, sizeof(config_path), 
         "%s/zeebo_config.ini", system_dir);
FILE *f = fopen(config_path, "r");

// Funciona em Windows, Linux, PSP, etc!
```

### Usar Callbacks RetroArch

```c
// Não acessar diretamente o filesystem
// Usar abstrações do RetroArch

// Gráficos:
video_cb(framebuffer, width, height, pitch);

// Áudio:
audio_batch_cb(samples, frames);

// Input:
input_poll_cb();
buttons = input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);

// Tudo abstrato, funciona em qualquer lugar!
```

---

## 📦 Buildbot do RetroArch (Distribuição Automática)

### Como Funciona

```
Quando seu núcleo está pronto:

1. Submit ao GitHub: github.com/libretro/libretro-cores
   └─ Pull request com seu código

2. Code review
   └─ Comunidade RevArch revisa

3. Merge
   └─ Seu código entra no repositório oficial

4. Buildbot compila automaticamente:
   ├─ Windows x86_64
   ├─ Linux x86_64 (várias distros)
   ├─ macOS Intel + Apple Silicon
   ├─ Android (múltiplas arquiteturas)
   ├─ Xbox One/Series X
   ├─ PlayStation 4
   ├─ Nintendo Switch
   ├─ PSP
   ├─ Dreamcast
   └─ ... e mais

5. Distribuição:
   └─ RetroArch Online Updater → Download zeebo
       Users baixam pre-compiled para sua plataforma
```

### Benefícios do Buildbot

```
✅ Compila para TODAS as plataformas
✅ Você não precisa ter xdev kit de Xbox/PS4
✅ Distribuição centralizada
✅ Atualizações automáticas
✅ Retrocompatibilidade gerenciada
```

---

## 🧪 Testing Multiplataforma

### Matriz de Teste

```
Testar em cada plataforma:

┌──────────────┬─────────┬──────────┬──────────┐
│ Plataforma   │ Carrega │ Compila  │ Funciona │
├──────────────┼─────────┼──────────┼──────────┤
│ Windows      │ ✅      │ ✅       │ ✅       │
│ Linux        │ ✅      │ ✅       │ ✅       │
│ macOS        │ ✅      │ ✅       │ ✅       │
│ Android      │ ✅      │ ✅       │ ✅       │
│ PSP          │ ✅      │ ✅       │ ✅       │
└──────────────┴─────────┴──────────┴──────────┘

Cada ✅ = núcleo funciona naquele lugar
```

### Script de Teste Automático

```bash
#!/bin/bash
# test_all_platforms.sh

platforms=("windows" "linux" "macos" "android" "psp")

for platform in "${platforms[@]}"; do
    echo "Testing $platform..."
    
    mkdir build_$platform
    cd build_$platform
    
    # Usar arquivo CMakeLists.txt específico
    cmake .. -DTARGET_PLATFORM=$platform
    
    if make; then
        echo "✅ $platform: PASS"
    else
        echo "❌ $platform: FAIL"
    fi
    
    cd ..
done
```

---

## 📋 Checklist de Portabilidade

```
Código:
[ ] Sem #include de headers específicos de SO
[ ] Sem paths hardcoded
[ ] Sem assembly
[ ] Tipos de dados portáveis (uint32_t, etc)
[ ] Endianness explícito

Build:
[ ] CMakeLists.txt funcional
[ ] Compila em Windows
[ ] Compila em Linux
[ ] Compila em macOS
[ ] Compila em Android (NDK)

I/O:
[ ] Usa RetroArch APIs (não acessar direto)
[ ] Paths obtidos via environ_cb
[ ] Sem hardcoding

Testes:
[ ] Testado em Windows
[ ] Testado em Linux
[ ] Testado em macOS
[ ] Testado em Android (emulador ou device)
[ ] Testou pelo menos 2 plataformas desktop

Documentação:
[ ] README com instruções de build
[ ] CONTRIBUTING.md se aceita PR
[ ] Plataformas suportadas documentadas
```

---

## 🎯 Roadmap de Portabilidade

### Fase 1-6 (Desenvolvimento)

```
Desenvolver em Windows ou Linux.
Manter código portável desde o início.
Não se preocupar com outras plataformas ainda.
```

### Fase 7 (Quando Núcleo Pronto)

```
[ ] Testar em segunda plataforma (Linux ou macOS)
[ ] Compilar sem erros em ambas
[ ] Testar com jogos em ambas
[ ] Documentar diferenças (se houver)
```

### Fase 8 (Quando Confiante)

```
[ ] Testar em Android (emulador Android Studio)
[ ] Testar em PSP/Dreamcast (se quiser)
[ ] Preparar para Buildbot
```

### Distribuição

```
[ ] PR ao libretro/libretro-cores
[ ] Code review
[ ] Merge
[ ] Buildbot compila para TUDO
[ ] Online Updater distribuiu automaticamente
```

---

## 💡 Dicas Finais

```
1. COMECE SIMPLES
   └─ Desenvolva em 1 plataforma
   └─ Manter portabilidade em mente

2. TESTE CEDO
   └─ Compile em segunda plataforma no meio do desenvolvimento
   └─ Pega problemas cedo

3. USE ABSTRAÇÕES
   └─ RetroArch APIs (não SO direto)
   └─ C standard (não extensions)

4. DOCUMENTE
   └─ Instruções de build por plataforma
   └─ Dependências (se houver)

5. COMPARTILHE
   └─ Buildbot faz o resto
   └─ Você não precisa compilar PSP/Xbox
```

---

## 🎯 Próximo Passo

Este documento faz parte da documentação completa de planejamento
(14 documentos no total). Para a lista completa e atualizada,
sempre consulte `00_INDICE.md` - é a fonte de verdade sobre quais
documentos existem e o status de cada um.

**FASE 1: PLANEJAMENTO = COMPLETA ✅**

Quando quiser começar a **Fase 0 (Setup + Estrutura)**, siga
`12_CHECKLIST_SEMANA1.md` passo a passo.
