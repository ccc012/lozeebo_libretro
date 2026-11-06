# Notas de engenharia reversa — Infuse A1 (Windows x64)

Análise estática do binário `Infuse.exe` (6.6 MB, PE32+, MinGW/libstdc++, símbolos C++ preservados)
com Ghidra 12.1.2 headless. Ferramentas em `C:\Users\Lucas\AppData\Local\Temp\opencode\re_infuse\`.

> Uso em clean-room: descobertas servem de referência de arquitetura para o nosso core;
> nenhum código foi copiado (licença do Infuse proíbe redistribuição do binário).

## Arquitetura geral

- **JIT ARM**: dynarmic (strings de assert `ctx.FPCR().RMode()`, `FP::RoundingMode::...`).
- **Gráficos**: GLFW + EGL/GLES 1.x (GLFW_Window, EGLConfig...), renderer por eventos OpenGL
  (Clear, Viewport, DrawText, DrawImage, DrawTriangles, CreateTexture/UpdateTexture/DeleteTexture).
- **Áudio**: `AudioBackendWaveOut`, `AudioObjectMIDI/PCM/MP3`, `AudioResampler`, soundfonts em
  `brew/soundfont/`, dr_mp3.
- **Input**: GLFW + gamepad mappings embutidos (strings de db `8BitDo...`, `Qanba...`).
- **Config**: `brew.language`, `BREW system language`, `Fullscreen rendering mode`.
- **RAM emulada**: 160 MB (`0xA000000`) alocada na inicialização; base guardada no global
  `[0x14062E460]`; endereços do jogo = ponteiro - base (o "entry point" é um offset na RAM).
- Paths de jogo: `mod/` (nome + path vindos da configuração), fontes em `./brew/font/`.

## Loader de MOD (wrapper proprietário)

`FUN_1400134D0` — carrega um MOD embalado:

1. Copia o blob (arquivo) para um buffer.
2. **XOR-decrypta** o blob com chave de 32 bytes (rolling key, param_5).
3. Valida hash do resultado (função estilo SHA, seed de 16 bytes em `_DAT_1404F4580`,
   blocos de 64 bytes: `FUN_140014130`); se o hash de 32 bytes != chave, `exit(0)`.
4. Registra o módulo com vtable `PTR_FUN_1405B7800` e chama `FUN_140011B10`.

### Header do MOD (parse em `FUN_140011B10`)

Campos lidos na ordem (big-endian):

| offset | tam | valor esperado | campo |
|--------|-----|----------------|-------|
| 0      | 16  | 0x57455242 "BREW" + byte 0x01 | magic + versão |
| +16    | 2   | 3              | (formato) |
| +18    | 2   | 0x28 (40)      | header size |
| +20    | 4   | —              | (flags?) |
| +24    | 4   | —              | (?) |
| +28    | 4   | —              | (?) |
| +32    | 4   | —              | (?) |
| +36    | 4   | —              | (?) |
| +40    | 2   | —              | (?) |
| +42    | 2   | —              | (?) |
| +44    | 2   | count segmentos | nº de segmentos |
| +46    | 2   | —              | (?) |
| +48    | 2   | —              | (?) |
| +50    | 2   | —              | (?) |

Cada segmento: 32 bytes lidos (`FUN_1403D7E40` em blocos 4+4+4+4...), armazenado como entrada
de 48 bytes num vector (provavelmente {addr, size, flags, ...}).

## Resolução de símbolos

`FUN_140011A90` — lookup por nome numa tabela de símbolos:
entradas de 0x38 bytes; nome em +0x10, tamanho em +0x18, tipo/flags em +0x0C,
**valor/endereço em +0x30**.

Nomes de seção sintética (strings em `.rdata`): `BREWLoader`, `BREWImports`, `BREWSection`.
O entry point é resolvido pelo símbolo **`BREWMainEntry`** (ref de `0x140076B0A` no
inicializador), depois subtrai-se a base da RAM → endereço ARM do entry point.

## Inicialização do jogo (método virtual `FUN_140076840`)

1. Inicializa objetos internos (vtables: IShell-like, renderer, input...).
2. `malloc(0xA000000)` — 160 MB; `[0x1404EF2D0] = 0xA000000`, `[0x1404EF2D4] = 0x80000`.
3. Carrega o MOD (`FUN_1400134D0`); se falhar: "Failed to load the BREW subsystem".
4. Lookup `BREWMainEntry` (`FUN_140011A90`), subtrai base da RAM.
5. Monta path `"mod/"` + nome, chama setup (`FUN_14000A6C0`, `FUN_14000C1F0`, `FUN_14000BF40`).

## Observações para o nosso core

- O `.mod` real do Zeebo (ex.: `tests/roms/real_ddragon_game.mod`, 462748 bytes) começa com
  **código ARM direto** (prologue `04 E0 2D E5 ...`), sem magic BREW — o wrapper XOR/BREW é
  coisa do Infuse (empacotador próprio), não do hardware/ROMs originais.
- CLSID de jogo (0x0106C411 etc.) **não** aparece no binário do Infuse como constante:
  a API é resolvida por nome de símbolo na tabela (diferente da nossa abordagem por CLSID).
- Não há tabela de CLSIDs AEE no binário; `0x01001001` (AEECLSID_DISPLAY) encontrado em
  0x5877DC do arquivo = região de dados de exceção (.pdata) — falso positivo.

## Caminhos usados na sessão

- `re_infuse\infuse\Infuse.exe` — binário.
- `re_infuse\ghidra_project\InfuseRE` — projeto Ghidra.
- Scripts: `scripts\*.java` (ExportFuncs, ExportStrings, ForceDecompile, DecompAt,
  DisasmRange, RefsArg, Callers, ListNear, StringsNear, MapRefsToFuncs...).
- Outputs: `all_funcs.txt`, `ghidra_strings.txt`, `string_to_func.txt`, `decomp_*.txt`,
  `disasm_*.txt`, `refs_*.txt`, `callers_*.txt`, `cir_*.txt`, `imms.txt`, `clsids.txt`.

## Funções-chave mapeadas

| Função | Papel |
|--------|-------|
| 1400134D0 | Loader de MOD (copia, XOR-decrypt, valida hash, registra) |
| 140014130 | Hash de validação (blocos 64B, seed 16B) |
| 140011B10 | Parse do header MOD (magic "BREW", segmentos) |
| 140011A90 | Lookup de símbolo por nome (tabela 0x38) |
| 140076660 | Ctor de objeto de jogo (nome + path, 0xA0 bytes, vtable 1405B81F0) |
| 140076840 | Init do emulador: 160MB RAM, load MOD, BREWMainEntry, path mod/ |
| 140076470 / 1400764D0 | Dtors da classe de jogo |
| 1400708C0 | Iteração de lista de jogos (cria objetos de jogo) |
| 14007B920 | Interpreter de AST (UI/script; string "initializer for module") |
| 140028D90 | Leitor de módulo/stream (bits, "bad file") |
| 140485E70 | Init global: 32MB buffer, seeds de configuração |
| 14000A6C0 | Setup de região de memória (chamado com base da RAM) |
