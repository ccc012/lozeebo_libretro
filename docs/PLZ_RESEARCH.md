# Pesquisa: formato de asset comprimido "PLZP" (Zeebo)

Data: 2026-11-04  
Escopo: formato de assets comprimidos encontrados como membros `.mp3`/`.wav` dentro do container `data.vfs` (FUFS) do Zeebo.  
Status: **formato identificado e validado** (sem decompressor dedicado encontrado nas referências).

## 1. Resumo

- O magic que o usuário observou como `PLZPL` (`50 4C 5A 50 4C`) **não é um magic de 5 bytes**. O magic real é **`PLZP` (`50 4C 5A 50`)**, 4 bytes; o 5º byte (`4C`) é apenas o byte baixo do campo `tamanho_descomprimido` daquele asset específico (`menu/sfx/music.mp3`).
- O formato é um pequeno wrapper de **4 + 4 + 4 = 12 bytes de cabeçalho** seguido de um stream **zlib** (`78 DA ...`, deflate com cabeçalho zlib).
- Campos (little-endian):
  1. `magic` = `"PLZP"` (4 bytes)
  2. `original_size` = tamanho descomprimido (uint32 LE)
  3. `compressed_size` = tamanho do payload zlib (uint32 LE)
  4. payload zlib de `compressed_size` bytes
- Validação: no `tests/roms/data.vfs` (FUFS, 448 membros) existem **137 membros PLZP**, todos inflam corretamente com zlib padrão e conferem `original_size`; 0 falhas. Exemplo `menu/sfx/music.mp3` infla para um MP3 válido (ID3).
- **Nenhum decompressor dedicado foi encontrado** no Zeemu (GPLv3) nem no restante do repositório. A descompressão é feita por zlib/miniz padrão (o próprio core já carrega `miniz`/`tinfl`).

## 2. Buscas realizadas

### 2.1 No repositório (código e referências)

Termos procurados (case-insensitive): `PLZ`, `plz`, `PLZPL`, `zeis`, `ZEIS`, `decompress`, `PLZP`, `FUFS`, `vfs`.

Resultados relevantes:

- `src/brew/ifile.c`: implementa o container **FUFS** (`data.vfs`) e janelas **GGZ** (gzip). Carrega `data.vfs` (magic `FUFS`, TOC em offset 12 com entradas de 12 bytes `{offset, hash, size}`), serve membros por hash de nome. **Não há tratamento de `PLZP`**: o membro é servido bruto.
- `src/audio/miniz.c` / `miniz_tinfl.h` / `miniz.h`: zlib/deflate padrão já disponível no core.
- `src/brew/imedia_util.c`: ao criar mídia a partir de nome de arquivo (`MMD_FILE_NAME`), tenta `fopen(g_base_dir/filename)` diretamente; **não consulta o VFS nem detecta PLZP**.
- `reference/zeemu-main`: nenhuma ocorrência de `PLZ`/`PLZP`/`PLZPL` em código-fonte (apenas ruído binário em terceiros como ffmpeg/SDL).
- `reference/openzeebo-master`: apenas `ggzupk.py` (gzip via zlib), sem PLZ.
- `reference/ggzbrewtools-main`: gzip padrão.
- `reference/infuse-analysis`: binário `Infuse.exe` não contém strings de PLZ/vfs decompression (além de instruções AVX `vplzcnt*`).
- Histórico git: `PLZPL` aparece apenas em blobs de `data.vfs` (commits `a6ce71a`, `1ffb04d`); nenhum commit implementa decompressor PLZ.

### 2.2 Web

Tentativas de busca web falharam (HTTP 402 / indisponível no ambiente). Fetch do GitHub (busca `PLZP Zeebo`) exigiu autenticação e não retornou resultados úteis. Nenhuma documentação externa foi obtida; o formato foi derivado por engenharia reversa local.

## 3. Estrutura detalhada

### 3.1 Container FUFS (`data.vfs`)

Já implementado em `src/brew/ifile.c`:

| offset | tamanho | campo |
|--------|---------|-------|
| 0      | 4       | magic `"FUFS"` |
| 4      | 4       | (reservado/versão, `0x814B9429` no sample) |
| 8      | 4       | `count` (número de membros) |
| 12     | 4       | `toc_len` (tamanho do TOC) |
| 12     | 12×count| TOC: `{offset(4), namehash(4), size(4)}` LE |
| toc_len| ...     | dados dos membros |

Hash de nome: `h = h*67 + c - 0x71`, com `\`→`/` e minúsculas→maiúsculas. Confirmado: `menu/sfx/music.mp3` hasha para o membro 408.

### 3.2 Membro comprimido PLZP

Cabeçalho de 12 bytes + payload zlib:

| offset (no membro) | tamanho | campo |
|--------------------|---------|-------|
| 0                  | 4       | magic `"PLZP"` (`50 4C 5A 50`) |
| 4                  | 4       | `original_size` (uint32 LE) — tamanho descomprimido |
| 8                  | 4       | `compressed_size` (uint32 LE) — tamanho do payload |
| 12                 | `compressed_size` | stream zlib (`78 DA ...` = deflate + cabeçalho/adler zlib) |

Observação: em `menu/sfx/music.mp3` (`original_size = 0x000F054C`), os primeiros 5 bytes do membro são `50 4C 5A 50 4C`, o que explica a leitura inicial do magic como `PLZPL`. O 5º byte é coincidência (byte baixo de `original_size`).

### 3.3 Verificação empírica

Script de validação (não commitado) sobre `tests/roms/data.vfs`:

- 448 membros no VFS.
- 137 membros com magic `PLZP`.
- Para todos: `compressed_size == member_size - 12`; `zlib.decompress(payload)` tem comprimento exato `original_size`; 0 falhas.
- Primeiro byte do stream zlib: `0x78` em 137/137 casos.
- `original_size` varia de 280 a 1.579.911 bytes.
- Exemplo: membro `menu/sfx/music.mp3` (hash `0xE4C...`? ver nota) infla para MP3 com cabeçalho ID3 (`ID3\x03\x00...`), tamanho 984.396 bytes.

Nota: o hash do nome `menu/sfx/music.mp3` é `3838921845` (`0xE4C...`? hex: `0xE4C...`), índice 408 no TOC.

## 4. O que "PLZ" significa / origem

Não há documentação ou código de referência. Hipótese razoável: abreviação de "Packed LZ" (ou "PLZipped") usada pelo toolchain da Zeebo para embalar assets de mídia dentro do `data.vfs`, com zlib padrão. Apenas a camada de 12 bytes de cabeçalho é proprietária.

## 5. Recomendações de próximos passos

1. **Implementar helper de descompressão PLZP** no core (ex.: `src/brew/plz.c` ou função em `ifile.c`/`imedia_util.c`):
   - detectar magic `PLZP` nos primeiros 4 bytes;
   - ler `original_size`/`compressed_size`;
   - inflar com `tinfl_decompress_mem_to_mem` (miniz, já disponível) ou `zlib`;
   - retornar buffer mallocado de `original_size`.
2. **Integrar no VFS**: ao abrir membro PLZP, descomprimir uma vez e servir o buffer descomprimido (análogo ao que `ggz_build` faz para `.ggz`), em vez de servir bytes crus. Isso afeta `IFile_Read`/`Seek` (campo `total` passa a ser `original_size`).
3. **Integrar no sistema de mídia**: `imedia_util.c` (`MMD_FILE_NAME`) deve:
   - tentar resolver via VFS quando `fopen` falhar;
   - detectar PLZP e inflar antes de popular `m->addr`/`m->bytes`;
   - definir `rate` corretamente para MP3/WAV.
4. **Ferramenta offline** (opcional): script Python para extrair/inflar todos os PLZP de um `data.vfs` para validação de áudio (útil para testes de regressão).
5. **Testes**:
   - teste unitário inflando `menu/sfx/music.mp3` e validando header ID3;
   - teste de que todos os 137 membros PLZP do romset de referência inflam sem erro;
   - smoke test de áudio no emulador após integração.
6. **Licença**: zlib/miniz já é usado; nenhuma dependência nova. Manter nota de clean-room (não copiar código do Infuse/Zeemu).

## 6. Arquivos relevantes

- `src/brew/ifile.c` — FUFS/GGZ/VFS (linhas ~254–337 VFS; ~770–817 `asset_load`).
- `src/brew/imedia_util.c` — criação de mídia a partir de nome de arquivo.
- `src/audio/miniz.c`, `src/audio/miniz_tinfl.h` — inflater disponível.
- `tests/roms/data.vfs` — container de referência (448 membros, 137 PLZP).
- `tests/vfs_analyze.py` — análise do TOC/hash do FUFS.
- `docs/RE_INFUSE_NOTES.md` — notas de RE do Infuse (contexto).