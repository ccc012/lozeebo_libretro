# 🎮 Zeebo LibRetro Emulator

**Emulador de código aberto para o console Zeebo, compatível com LibRetro/RetroArch**

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)]()
[![License](https://img.shields.io/badge/license-MIT-blue)]()
[![Phase](https://img.shields.io/badge/phase-0%20--%20Skeleton-yellow)]()

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

**Fase 0: Setup & Skeleton** ✅

- [x] Estrutura de pastas
- [x] Header LibRetro obtido
- [x] Skeleton core (28 funções) implementado
- [x] Build system configurado
- [x] Compilação bem-sucedida
- [x] Documentação completa

**Próxima**: Fase 1 - CPU ARM (em breve)

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

**Mais detalhes**: `docs/TESTE_RETROARCH.md`

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
- `SKELETON_CHECKLIST.md` - Checklist detalhado
- `TESTE_RETROARCH.md` - Guia de teste
- `FINAL_REPORT.md` - Relatório completo
- `PROGRESS.md` - Timeline de desenvolvimento

---

## 📄 Licença

MIT License
