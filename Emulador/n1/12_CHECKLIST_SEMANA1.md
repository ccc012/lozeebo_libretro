# ✅ Checklist Prático - Semana 1-2 (Fase 0)

> Lista de tarefas concretas para marcar conforme completa. Use este documento como seu "controle de progresso" diário.

---

## 📌 Como Usar Este Checklist

```
Este documento é DIFERENTE dos outros.
Não é teoria - é uma lista de AÇÕES CONCRETAS.

Marque [x] conforme completa cada item.
Se travar em algum item, anote o problema em docs/NOTES.md.

Ao final, todos os itens devem estar marcados = Fase 0 completa.
```

---

## 🖥️ Bloco 1: Ambiente e Ferramentas

```
[ ] Sistema operacional definido (Windows/Linux/Mac)
[ ] Compilador instalado (gcc ou clang)
    Verificar: gcc --version
[ ] Make instalado
    Verificar: make --version
[ ] Git instalado
    Verificar: git --version
[ ] GDB instalado (opcional nesta fase, mas recomendado)
    Verificar: gdb --version
[ ] Editor de código instalado (VSCode recomendado)
[ ] RetroArch instalado e abre normalmente
```

📄 Referência detalhada: `06_TOOLCHAIN_SETUP.md`

---

## 🧪 Bloco 2: Teste de Setup (Hello World)

```
[ ] Criar arquivo teste.c com "Hello World" em C
[ ] Compilar: gcc teste.c -o teste
[ ] Executar: ./teste
[ ] Ver mensagem impressa corretamente
```

Se este bloco falhar, **pare aqui** e resolva o ambiente antes de continuar
(veja seção "Problemas Comuns de Setup" em `06_TOOLCHAIN_SETUP.md`).

---

## 🗂️ Bloco 3: Estrutura do Projeto

```
[ ] Pasta zeebo_libretro/ criada
[ ] Subpastas criadas (src/core, src/cpu, src/memory, src/loader,
    src/brew, src/gpu, src/audio, src/input, src/debug, include,
    tests/roms, docs, build)
[ ] git init executado dentro da pasta
[ ] Arquivo .gitignore criado (build/, *.o, *.so, *.dll, tests/roms/*.mod)
[ ] Primeiro commit feito (git commit -m "Estrutura inicial do projeto")
```

📄 Referência: `07_ESTRUTURA_PASTA.md`

---

## 📥 Bloco 4: Dependências Externas

```
[ ] libretro.h baixado e salvo em include/
[ ] Verificado que o arquivo não está corrompido (abre e tem conteúdo)
[ ] (Opcional nesta fase) Ghidra baixado, caso queira já começar
    a olhar arquivos MOD dos jogos alvo
```

📄 Referência: `06_TOOLCHAIN_SETUP.md`

---

## 📝 Bloco 5: Código Skeleton

```
[ ] src/core/libretro_core.c criado com todas as funções retro_*
[ ] Makefile criado na raiz do projeto
[ ] make executado sem erros
[ ] Arquivo .so/.dll/.dylib gerado com sucesso
[ ] git commit feito ("Skeleton do núcleo compila")
```

📄 Referência: `11_PROTOTIPO_SKELETON.md`

---

## 🎮 Bloco 6: Validação no RetroArch

```
[ ] Núcleo copiado para pasta cores/ do RetroArch
[ ] RetroArch aberto
[ ] "Zeebo" aparece na lista de núcleos (Load Core)
[ ] Um arquivo de teste (.mod fake) carrega sem crash
[ ] Tela preta aparece (confirma que video_cb funciona)
[ ] Log mostra "[Zeebo] retro_init() chamado" no terminal
```

---

## 📄 Bloco 7: Documentação Interna

```
[ ] docs/PROGRESS.md criado (modelo abaixo)
[ ] docs/NOTES.md criado (para anotações técnicas futuras)
[ ] README.md criado na raiz com descrição breve do projeto
```

### Modelo de PROGRESS.md

```markdown
# Progresso do Projeto Zeebo LibRetro

## Semana 1-2 (Fase 0: Setup & Estrutura)
### Feito
- Ambiente de desenvolvimento configurado
- Estrutura de pastas criada
- Skeleton do núcleo compila
- RetroArch reconhece o núcleo "Zeebo"

### Estatísticas
- Instruções ARM implementadas: 0/150
- APIs BREW implementadas: 0
- Jogos rodando: 0/3
```

---

## 🎯 Checklist Consolidado (Visão Rápida)

```
┌─────────────────────────────────────────┬────────┐
│ Item                                     │ Status │
├───────────────────────────────────────────┼────────┤
│ Compilador instalado                     │ [ ]    │
│ Make instalado                           │ [ ]    │
│ Git instalado                            │ [ ]    │
│ RetroArch instalado                      │ [ ]    │
│ Hello World compila e roda               │ [ ]    │
│ Estrutura de pastas criada               │ [ ]    │
│ Git inicializado + primeiro commit       │ [ ]    │
│ libretro.h baixado                       │ [ ]    │
│ Skeleton (.c) criado                     │ [ ]    │
│ Makefile criado                          │ [ ]    │
│ Núcleo compila (.so/.dll)                │ [ ]    │
│ RetroArch reconhece o núcleo             │ [ ]    │
│ Carrega "jogo" fake sem crash            │ [ ]    │
│ PROGRESS.md / NOTES.md / README.md        │ [ ]    │
└─────────────────────────────────────────┴────────┘
```

Quando **todas as linhas** estiverem marcadas, a Semana 1-2 (Fase 0) está 100% concluída,
e você pode avançar oficialmente para a **Fase 1: CPU Core** (`04_EMULACAO_CPU_DETALHADA.md`).

---

## ⚠️ O Que Fazer se Travar

```
Trava no Bloco 1-2 (ambiente):
└─ Revisar 06_TOOLCHAIN_SETUP.md, seção "Problemas Comuns de Setup"

Trava no Bloco 5 (compilação do skeleton):
└─ Ler a mensagem de erro do gcc com atenção
   ├─ "undefined reference" → falta alguma função retro_*
   ├─ "libretro.h not found" → path errado no -I do Makefile
   └─ "cannot open shared object" → falta -fPIC ou -shared

Trava no Bloco 6 (RetroArch não reconhece):
└─ Verificar se o arquivo está na pasta certa
└─ Verificar se compilou para a arquitetura certa (32/64-bit)
└─ Reiniciar o RetroArch depois de copiar o núcleo
```

---

## 🎯 Próximo Passo

Com este checklist 100% completo:

→ Você tem oficialmente terminado a **Fase 0** do projeto.
→ Próximo documento a seguir: `04_EMULACAO_CPU_DETALHADA.md`, começando pela
  Sub-fase 1.1 (Estrutura da CPU) do `03_PLANO_DESENVOLVIMENTO.md`.
