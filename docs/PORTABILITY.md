# Portabilidade

Este documento registra o estado de preparo do `zeebo_libretro` para plataformas suportadas pelo ecossistema `libretro`/`RetroArch`.

## Objetivo

O projeto nao tenta prometer suporte imediato a todo alvo onde o `RetroArch` roda. O objetivo aqui e deixar:

- o **codigo do core** o mais neutro de plataforma possivel;
- o **build system** pronto para toolchains cruzados comuns;
- a **organizacao do repositorio** preparada para futuras ports sem reescrever a base.

## Estado atual

### Ja preparado no repositorio

- `CMakeLists.txt` funcional para o core completo.
- `CMakePresets.json` com presets para:
  - `desktop-debug`
  - `desktop-release`
  - `desktop-smoke`
  - `android-arm64`
  - `android-armv7`
  - `android-x86_64`
  - `emscripten`
  - `uwp`
- `Makefile` atualizado para desktop (`Linux`, `macOS`, `Windows` via MinGW/MSYS2).
- `tests/libretro_smoke.c` sem dependencia exclusiva de `Win32`; usa `LoadLibrary` no Windows e `dlopen` fora dele.
- Definicoes de compilacao por familia de plataforma no CMake:
  - `ZEEBO_PLATFORM_DESKTOP`
  - `ZEEBO_PLATFORM_MOBILE`
  - `ZEEBO_PLATFORM_CONSOLE`
  - `ZEEBO_PLATFORM_WEB`
- Definicoes de arquitetura:
  - `ZEEBO_ARCH_32BIT`
  - `ZEEBO_ARCH_64BIT`

### Ainda dependente de teste real

- `Linux`
- `macOS`
- `Android`
- `UWP/Xbox Developer Mode`
- `Emscripten/Web`

### Fora do escopo imediato

Estas plataformas normalmente exigem SDKs proprietarios, toolchains externos ou integracao especifica do frontend. O core esta mais preparado para elas, mas nao ha como validar tudo so com este workspace:

- `PS Vita / PSP / PS2 / PS3 / PS4`
- `Switch`
- `Wii / Wii U / GameCube`
- `3DS / 2DS`
- `iOS / tvOS`
- `Haiku`
- handhelds niche (`OpenDingux`, `RetroFW`, `Miyoo`, etc.)

## Como usar os presets

### Desktop

```powershell
cmake --preset desktop-release
cmake --build --preset desktop-release
```

### Smoke test host

```powershell
cmake --preset desktop-smoke
cmake --build --preset desktop-smoke
```

### Android

Requer `ANDROID_NDK` no ambiente.

```powershell
cmake --preset android-arm64
cmake --build --preset android-arm64
```

### Web / Emscripten

Requer `EMSDK` no ambiente.

```powershell
cmake --preset emscripten
cmake --build --preset emscripten
```

### UWP

```powershell
cmake --preset uwp
cmake --build --preset uwp
```

## O que falta para ports mais dificeis

### Consoles e ports homebrew

Normalmente falta um ou mais destes itens:

- SDK/toolchain do alvo
- ajustes de empacotamento do frontend
- limites de memoria mais agressivos
- revisao de performance da CPU interpretada
- revisao de E/S de arquivos e paths sandboxed
- testes reais de audio/video/input no hardware

### iOS/tvOS

- preset especifico ainda nao foi adicionado
- possivel necessidade de ajustar output/bundle e assinatura
- smoke test dinamico nao e prioridade nesse alvo

### Web

- o preset existe, mas o core ainda nao foi validado em `Emscripten`
- pode ser necessario revisar loading de conteudo e tamanho de memoria
- performance do interpretador ARM pode ser insuficiente para jogos reais

## Diretrizes de codigo para manter a portabilidade

- evitar APIs diretas de `Windows`, `POSIX` ou SDKs de console dentro de `src/`
- preferir C padrao e a API `libretro`
- manter caminhos internos normalizados com `/`
- isolar qualquer necessidade futura de plataforma no build system ou em camadas pequenas
- nao assumir desktop, teclado fisico, mouse ou sistema de arquivos amplo

## Resumo honesto

O repositorio agora esta **preparado** para mais plataformas do que antes, principalmente no nivel de build e organizacao. Isso nao significa que todos os ports vao funcionar de primeira. Significa que, quando houver toolchain e hardware/frontend disponiveis, o core exigira menos retrabalho estrutural.
