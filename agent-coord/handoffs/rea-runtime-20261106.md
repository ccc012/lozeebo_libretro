# Handoff — rea-runtime — 2026-11-06

## Resultado

Display runtime/Data East escolhido por maior ROI. Contrato clean-room criado em `docs/REA_INFUSE_DISPLAY_RUNTIME_CONTRACT.md`. Nenhum arquivo `src/` alterado.

## Evidência

- REA: 6.8.0.
- Infuse: `G:\Meu Drive\Documentos obi\Projeto\Emulador\re_infuse\bin\Infuse.exe`.
- Projeto: Ghidra 12.1.4 `re_infuse/ghidra_project/InfuseRE`.
- Runtime host: HAL guest → eventos tipados → renderer OpenGL.
- Handlers prioritários: DrawImage `0x140049c10`, DrawTriangles `0x140049860`, DrawText `0x14004a450`.
- REA provider Ghidra indisponível: `GHIDRA_INSTALL_DIR` ausente. Logs: `C:\Users\Lucas\AppData\Local\Temp\opencode\rea-runtime\`.

## Próxima ação

1. Configurar `GHIDRA_INSTALL_DIR` para Ghidra 12.1.4.
2. Rodar REA `function` nos três endereços.
3. Extrair somente payload, ownership, ordenação; cruzar com trace Data East.
4. Alterar `src/` somente após identificar violação concreta do contrato.

## Merge

Branch `front/rea-runtime`; docs/oracle only; estado `ready-merge`.
