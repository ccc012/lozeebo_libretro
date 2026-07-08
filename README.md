# 🎮 Zeebo LibRetro Emulator

**Emulador de código aberto para o console Zeebo, compatível com LibRetro/RetroArch**

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)]()
[![License](https://img.shields.io/badge/license-GPLv3-blue)]()
[![Phase](https://img.shields.io/badge/phase-advanced%20HLE%20core-orange)]()

---

## 📝 Sobre

O Zeebo foi um console Java-based lançado pela Samsung em 2009. Este projeto implementa um emulador de código aberto que:

- ✅ Emula a CPU ARM
- ✅ Implementa APIs BREW (High-Level Emulation)
- ✅ Suporta carregamento de ROMs (.MOD)
- ✅ Integra-se com RetroArch/LibRetro
- ✅ Compila em Windows, Linux e macOS

---

## 🚀 Status Atual

**Base funcional avançada do core Zeebo** ✅

- [x] Estrutura de pastas
- [x] Header LibRetro obtido
- [x] Core LibRetro integrado com CPU, memória, BREW, loader, input, áudio e vídeo
- [x] ROMs de teste e ROMs reais de referência presentes
- [x] Documentação de arquitetura e estratégia consolidada
- [x] Build validado em Release|x64 (Visual Studio 2022)
- [x] Pac-Mania e Zeebo Family Pack (ROMs reais) avançam via AEEMod_Load/IModule_CreateInstance
- [ ] Bug de CPU em investigação durante a saída do CreateInstance (ver `docs/PROGRESS.md`)

**Próximo**: ver `docs/PROGRESS.md` para o estado exato do bug atual e os próximos passos

---

## 📦 Build

### Windows (Visual Studio 2022)

```bash
msbuild zeebo_libretro.sln /p:Configuration=Release /p:Platform=x64
# Resultado: x64/Release/zeebo_libretro.dll
```

### Linux/macOS (Make)

```bash
make                    # Compilar
make clean              # Limpar
make install            # Instalar
```

---

## 🎮 Como Usar

1. Copie `x64/Release/zeebo_libretro.dll` para `C:\RetroArch\cores\`
2. Abra RetroArch
3. Load Core → procure "Zeebo"
4. Load Content → selecione arquivo .mod

**Mais detalhes**: `docs/TESTING.md`

## Licença e reutilização

O repositório está sob **GPLv3**. O projeto também pode incorporar código, ideias, adaptações ou trechos derivados de outros projetos compatíveis com a licença e com seus respectivos avisos de origem.

Fontes de referência: ver `docs/THIRD_PARTY.md` (Zeemu, Infuse, GGZ BREW Tools, fontes/soundfonts).

---

## 📂 Estrutura

```
src/core/          → Skeleton LibRetro ✅
src/cpu/           → CPU ARM (Fase 1)
src/memory/        → Memória (Fase 1)
src/loader/        → Carregador (Fase 2)
src/brew/          → APIs BREW (Fase 3)
docs/              → Documentação
tests/             → Testes
```

---

## 📚 Documentação

Veja `docs/` para:
- `PROGRESS.md` - Timeline de desenvolvimento e estado atual (bugs, próximos passos)
- `TESTING.md` - Guia de teste (ROMs reais, smoke test, como ler um crash)
- `THIRD_PARTY.md` - Licenças e atribuições de material de terceiros
- `PLANNING_ARCHIVE.md` - Arquivo do racional de design original (pré-desenvolvimento)

---

## 📄 Licença

GPLv3
