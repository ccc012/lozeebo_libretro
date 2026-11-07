# Pacotes completos ZIP/7z — handoff

Branch `fix/content-packages`, base `90c11791b574cf98273fa3035b6283836b79e75a`.
Implementação apenas nesta worktree; sem deploy, push ou merge.

## Contrato

- `need_fullpath=true`, `block_extract=true`, extensões incluem `7z` no core e em `dist/info/zeebo_libretro.info`.
- Abrir ZIP/7z prepara pacote inteiro, preservando `mod/<id>/`, `mif/`, assets e CLSID. Seleção automática exige exatamente um `.mod`; seleção explícita aceita `GET_GAME_INFO_EXT.archive_path/archive_file` e `archive#member`. MIF não vira executável selecionado.
- `.mod` direto mantém loader anterior. `retro_reset` reutiliza bytes e caminho MOD preparado.
- Erro de pacote retorna `false`, registra motivo e envia `RETRO_ENVIRONMENT_SET_MESSAGE`; não retorna sucesso com um pacote bruto.

## Dependência / limites deliberados

Windows desktop; executável instalado fixo `C:\ProgramData\chocolatey\bin\7z.exe`.
Nenhuma busca em PATH, shell, PowerShell ou execução de dados guest. Não há instalação automática.
Outras plataformas retornam erro explícito para pacote; extração manual completa + MOD continua alternativa.
Dependência nativa nova de link: Windows BCrypt SHA256.

8192 entradas, 512 MiB por membro, 2 GiB pacote de origem / soma extraída, listing 8 MiB, timeout 120s por processo.
Nomes ambíguos, absolutos, traversal, ADS, dispositivos Windows, links e duplicatas case-insensitive são rejeitados.
7-Zip produz somente stdout. Core cria cada arquivo `CREATE_NEW`, controla bytes escritos e diretórios, impede reparse points existentes.
Pacotes sólidos grandes podem demorar na primeira abertura: decoder invocado por arquivo para não delegar escrita de caminhos guest ao extrator.
Caminho MOD final deve caber em 512 bytes, limite já existente no core. Pacotes criptografados não são suportados.

## Cache

`%TEMP%\LOZeebo-content-v1\<SHA256(caminho absoluto UTF-16 + bytes origem)>`.
Origem fica aberta sem compartilhamento de escrita/delete durante preparação.
Toda abertura calcula identidade da origem, lista membros, valida tamanho e SHA256 de cada arquivo preparado.
Cache hit não extrai nem reescreve arquivos. FP observado ~0,16–0,20s; ZIP pequeno ~0,07s.
Manifesto escrito/flush após extração; diretório staging publicado com rename atômico.
Staging parcial removido sem seguir junctions; originais nunca apagados.
Cache corrompido é recusado com erro explícito: remover apenas o diretório hash gerado permite reconstrução.
Não há coleta automática de caches antigos. Mesmo usuário local malicioso com corrida de substituição de diretórios não é sandboxed; checagens de reparse points não equivalem a isolamento contra processos host privilegiados.

## Provas executadas

```powershell
cmake -S . -B build-content -DZEEBO_BUILD_SMOKE_TEST=ON
cmake --build build-content --config Release --parallel 8
python tests/check-content-packages.py
python tests/check-content-frontend.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools/fp-gate/check-content-path.ps1 -Mod 'D:\Games\Zeebo - Zeebo\Zeebo Family Pack (Brazil) (Es,Pt).7z' -BuildDir build-content
powershell -NoProfile -ExecutionPolicy Bypass -File tools/fp-gate/check-content-path.ps1 -Mod 'C:\Users\Lucas\AppData\Local\Temp\opencode\content-repro\fp\mod\277229\game.mod' -BuildDir build-content
```

Todos PASS. Testes incluem ZIP completo/MIF/assets, cache hit sem mudar mtime, mudança de origem/conteúdo, corrupção de cache, traversal/absolutos/ADS/devices/symlink, duplicatas, múltiplos MOD, limites count/tamanho, FP real 7z.
Frontend smoke cobre pacote bruto, `archive#mod` e `GET_GAME_INFO_EXT` com caminho temporário inexistente.
FP 900 frames em todos os caminhos: PPM SHA256 `DC43FEA183E9930E6C6C24F1012680CBCC9CBD754CEFDA39BE98A31F258947C0`, `audio_nonzero=931855`.

RED verificado recompilando base `90c1179` em `artifacts/content-packages/red-base/build` (sem trocar branch): o mesmo gate com 7z bruto falha, SHA256 `A6087EC5178C7619D8136DE2AA159DDE7161D56F9E4C3B899B7165935D0353D8`; core antigo retorna sucesso mesmo sem conteúdo correto.
Build novo `build-content`, sem reutilizar CMakeCache herdado. Aviso MSBuild MSB8029 por build sob TEMP; probe tem avisos CRT C4996, não erros.

Não houve teste visual no RetroArch instalado; não foi modificado. `.info` novo precisa acompanhar DLL numa futura instalação autorizada, senão frontend pode preservar comportamento antigo de seleção/extração.
Não foram testados todos os 68 jogos, cache contra desligamento físico abrupto, nem plataformas não Windows. CPU/controle/gameplay não alterados.

## Documentação oficial consultada

`https://raw.githubusercontent.com/libretro/libretro-common/master/include/libretro.h`
(campos `retro_system_info.need_fullpath`, `block_extract`; `RETRO_ENVIRONMENT_GET_GAME_INFO_EXT`; `retro_game_info_ext.archive_path/archive_file/file_in_archive`).
`need_fullpath` entrega caminho em vez de depender do buffer do frontend; `block_extract` pede que o core receba o arquivo comprimido; informação estendida identifica origem/membro caso frontend ainda forneça conteúdo interno.
Skill find-docs carregada. CLI Context7 neste ambiente devolveu `clean — nothing to commit` sem resultado documental; confirmação feita no header oficial remoto e cópia local.
