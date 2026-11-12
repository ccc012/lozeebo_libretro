# LOZeebo — Core Libretro para Zeebo

> **Status do Projeto: Encerrado e Preservado (2026-11-12)**
> O desenvolvimento ativo deste projeto foi encerrado formalmente por decisão do autor. A base de código, os testes sintéticos, as ferramentas de regressão e a pesquisa de engenharia reversa estão preservados para fins históricos e de estudo. Não inclui jogos comerciais nem oferece garantia de compatibilidade com o catálogo completo do console.

---

## 1. Visão Geral

**LOZeebo** (`zeebo_libretro`) é uma implementação experimental em **C puro (C99/C11)** de um emulador em alto nível (HLE) para o console brasileiro **Zeebo** (lançado em 2009 pela Tectoy e Qualcomm), projetado como um núcleo compatível com a API **Libretro** (RetroArch e outros frontends).

O console original executava jogos compilados para a plataforma móvel **BREW (Binary Runtime Environment for Wireless)** da Qualcomm sobre um processador ARM1136EJ-S (SoC Qualcomm MSM7201A). Em vez de simular em baixo nível os controladores de rádio e o microkernel proprietário REX/BREW, o LOZeebo implementa um interpretador de CPU determinístico integrado a uma camada de interceptação HLE das APIs da **AEE (Application Execution Environment)**.

---

## 2. Arquitetura dos Subsistemas

O código-fonte está estruturado modularmente em `src/`, sem dependências de frameworks pesados:

| Subsistema | Diretório | Descrição Técnica |
|---|---|---|
| **CPU** | `src/cpu/` | Interpretador de instruções ARMv6 e Thumb-1 (`execute_arm.c`, `execute_thumb.c`, `decode.c`, `flags.c`), com emulação precisa de barrel shifter e flag de saturação DSP (ARMv5TE). |
| **Memória** | `src/memory/` | Mapeamento de 64 MB de SDRAM contínua, heap dinâmico para alocações de jogos e interceptação de chamadas de sistema via traps virtuais HLE na faixa `0xFFFF0000`. |
| **BREW / AEE** | `src/brew/` | Reimplementação das interfaces COM-like do BREW: `IShell`, `IDisplay` (2D), `IBitmap`, `IFile`, `IImage` (PNG/JPEG/BMP), `IMemory`, `ISound`, `IMedia`, `ISource`, `IFont` e despacho de eventos (`AEEEvent`). |
| **Gráficos** | `src/gpu/` | Pipeline de rasterização 2D (`draw.c`, `framebuffer.c`) com suporte a modos ROP e transparência; suporte preliminar a 3D via OpenGL ES 1.1 / EGL (`egl_gl.c`). |
| **Áudio** | `src/audio/` | Mixer de áudio PCM estéreo em 44.1 kHz, decodificador IMA ADPCM com conversão de taxa de amostragem, MP3 via `dr_mp3` e síntese de arquivos MIDI via `TinySoundFont`. |
| **Entrada** | `src/input/` | Mapeamento de controle do Z-Pad (gamepad do Zeebo) para os botões e eixos da interface Libretro, tratando máscaras nativas de teclas e transições de estado. |
| **Carregador** | `src/loader/` | Carregador e parser de binários executáveis (`.mod`), metadados de aplicativo (`.mif`), arquivos de recursos compactados (`.bar`), contêineres `.pakz` e pacotes completos em ZIP/7z com cache SHA256. |
| **Core Libretro** | `src/core/` | Ponto de entrada padrão da especificação Libretro v1 (`retro_init`, `retro_load_game`, `retro_run`, callbacks de áudio, vídeo e entrada). |
| **Depuração** | `src/debug/` | Desmontador de instruções ARM/Thumb (`disasm.c`), rastreamento de execução (`trace.c`), medição de desempenho e logs estruturados. |
| **Bibliotecas Externas** | `src/extern/` | Cabeçalhos de bibliotecas de terceiros devidamente isolados (`stb_image.h` para decodificação de imagens). |

---

## 3. Estrutura do Repositório

O repositório foi totalmente reorganizado e saneado para separar código ativo, ferramentas canônicas e acervo histórico:

```
zeebo_libretro/
├── CMakeLists.txt              # Configuração canônica de build (CMake 3.10+)
├── CMakePresets.json           # Presets de compilação CMake
├── LICENSE                     # Licença pública GNU GPL-3.0
├── README.md                   # Esta documentação principal
├── dist/                       # Metadados de empacotamento
│   └── info/zeebo_libretro.info # Definição do núcleo para frontends Libretro
├── docs/                       # Documentação técnica e relatórios de pesquisa
│   ├── INDEX.md                # Índice temático detalhado
│   ├── ORGANIZACAO.md          # Mapa completo da organização e verificações
│   ├── ENCERRAMENTO.md         # Registro formal de encerramento do projeto
│   ├── LIMPEZA_FINAL.md        # Relatório de redução de espaço em disco
│   ├── THIRD_PARTY.md          # Inventário de terceiros, licenças e referências
│   └── historico/              # Documentos antigos arquivados (prompts, rodadas, frentes)
│       ├── raiz/               # Análises, status e briefings movidos da raiz
│       └── agent-coord/        # Histórico de coordenação multiagente e handoffs
├── src/                        # Código-fonte do núcleo (C99)
├── tests/                      # Harnesses de teste sintéticos e automação
│   └── roms/                   # Fixture sintética (test_draw.mod) e política de testes
├── tools/                      # Ferramentas ativas de teste e portões de qualidade
│   ├── regression.py           # Executor central da suíte de regressão
│   ├── REGRESSION.md           # Guia de uso e limites da suíte de testes
│   ├── check-layout.py         # Validador estrutural do layout do repositório
│   ├── capability-gate/        # Portão de capacidade e oráculo de saída
│   ├── dd-gate/                # Portão de verificação de gameplay de Double Dragon
│   ├── fp-gate/                # Portão de verificação de Family Pack
│   └── legado/                 # Scripts e arquivos de build antigos descontinuados
│       ├── build/              # Makefile e solução Visual Studio antigos
│       └── scripts/            # Scripts PowerShell de levantamentos históricos
└── preservacao/                # Arquivos privados de pesquisa, patches e Ghidra (no .gitignore)
```

> **Aviso de Separação Documental:** Todo o acervo documental em Markdown, notas de planejamento, enciclopédia técnica e registros de sessões anteriores estão organizados de forma centralizada e sem redundâncias no vault Obsidian (`G:\Meu Drive\Documentos obi\Projeto\Emulador`).

---

## 4. Instruções de Compilação (Build Canônico)

O sistema de build canônico e suportado é o **CMake**.

### Ambiente Recomendado
- **Sistema Operacional:** Windows 10 / Windows 11 (x64)
- **Compilador:** Microsoft Visual Studio 2022 (MSVC v143 ou superior)
- **Ferramentas:** CMake 3.10+ e Python 3.10+

### Passo a Passo de Compilação
Abra o terminal do PowerShell e execute:

```powershell
# 1. Configurar o projeto com compilação dos testes habilitada
cmake -S . -B build/check -DZEEBO_BUILD_SMOKE_TEST=ON

# 2. Compilar em configuração Release
cmake --build build/check --config Release
```

### Artefatos Produzidos
Ao término da compilação, os seguintes binários são gerados em `build/check/Release/`:
- `lozeebo_libretro.dll`: O núcleo dinâmico pronto para uso no RetroArch.
- `audio_adpcm_rate.exe`: Harness de teste do decodificador ADPCM e taxas de amostragem.
- `input_transitions.exe`: Harness de teste da máquina de estados de entrada.
- `gles_composition.exe`: Harness de teste do rasterizador e modos de composição GLES (`GL_REPLACE` / `GL_MODULATE`).
- `pakz_reader.exe`: Harness de validação do leitor de contêineres compactados PAKZ.
- `libretro_smoke.exe`: Teste de inicialização básica da API Libretro sem interface gráfica.
- `content_package_check.exe` e `content_run7_check.exe`: Testes de integridade de pacotes.

---

## 5. Como Testar e Verificar

O projeto inclui testes determinísticos que dispensam ROMs comerciais no checkout:

```powershell
# 1. Verificar integridade estrutural e ausência de resíduos
python tools/check-layout.py

# 2. Executar self-test do runner de regressão
python tools/regression.py --self-test

# 3. Executar harnesses unitários dos subsistemas
.\build\check\Release\audio_adpcm_rate.exe
.\build\check\Release\input_transitions.exe
.\build\check\Release\gles_composition.exe
.\build\check\Release\pakz_reader.exe
```

Todos os 5 comandos acima são validados automaticamente e devem retornar código de saída `0` (PASS). Para detalhes sobre a suíte completa de regressão e dependências locais de fixtures externas, consulte [`tools/REGRESSION.md`](tools/REGRESSION.md).

---

## 6. Estado Técnico e Evidências Concretas

Relatórios históricos usavam nomenclaturas variadas como "boot", "jogável" ou "variado". O status técnico real comprovado por testes é o seguinte:

- **Double Dragon:** Possui avanço comprovado com controle real de gameplay na Fase 1 (`TEMPO 60`), rastreando a paleta de cores exclusiva do personagem Billy Lee (`RGB 0,49,156`) durante movimentação, soltura de botões, direção oposta e execução de golpe, sustentado por 1.900 frames com áudio contínuo (`docs/PROGRESS_dd_gameplay.md`). Não há comprovação de conclusão total de fases.
- **Zeebo Family Pack:** Superou a tela preta inicial de boot e a película azul sobre os personagens na roda de seleção através da correção do modo `GL_REPLACE` em `egl_gl.c` (`docs/PROGRESS_fp_blue.md`). Minigames 3D foram testados sob harness controlado.
- **Ovos:** Inicialização do starfield e decodificação do mixer de áudio corrigida.
- **Pacotes Completos:** O leitor de pacotes ZIP/7z isola processos de descompressão em Windows Job Objects com watchdog de 120 segundos e validação SHA256 contra adulterações (`docs/CONTENT_PACKAGES.md`).

---

## 7. Licença e Restrições de Publicação

- **Código do Core:** Licenciado sob a **GNU General Public License v3.0 (GPL-3.0)**. Consulte o arquivo [`LICENSE`](LICENSE) para o texto completo da licença.
- **Componentes de Terceiros Incorporados:**
  - `stb_image.h`: Domínio Público / MIT.
  - `dr_mp3.h`: Domínio Público / MIT-0.
  - `tsf.h`: Licença MIT (atribuição SFZero).
  - `tml.h`: Licença zlib.
  - `miniz`: Licença MIT / notas históricas de domínio público.
  - `libretro.h`: Licença MIT.
  - Consulte o inventário canônico de proveniência em [`docs/THIRD_PARTY.md`](docs/THIRD_PARTY.md).

### Alerta Obrigatório sobre o Histórico Git
> **NÃO PUBLIQUE ESTE REPOSITÓRIO OU FAÇA PUSH INDISCRIMINADO PARA SERVIÇOS PÚBLICOS (GITHUB/GITLAB).**
> Embora o diretório de trabalho (`working tree`) atual tenha sido higienizado e não contenha nenhuma ROM comercial, cópias compactadas de jogos ou binários proprietários de terceiros, o **histórico antigo do Git (commits e árvores anteriores)** ainda armazena objetos que faziam parte de snapshots passados.
> Caso o repositório venha a ser disponibilizado publicamente no futuro, será obrigatório realizar um saneamento prévio e destrutivo do histórico (usando ferramentas como `git-filter-repo` ou BFG Repo-Cleaner) acompanhado de uma revisão de direitos autorais.
