# Frente: rea-runtime

**Objetivo:** Oracle comportamental Infuse via REA+Ghidra (sem copiar código).  
**Estado:** ready-merge  
**Owner:** docs only por padrão; src só com contrato + mestre  
**Tooling:** REA 6.8.0; Ghidra 12.1.4; Infuse stripped

## Feito

- `docs/REA_INFUSE_ITHREAD.md`
- `docs/REA_INFUSE_DISPLAY_GL.md`
- Contratos aplicados (flag=0; IDisplay/QEGL gaps)
- Escolha ROI: Display runtime/Data East supera Alice pós-HID.
- `docs/REA_INFUSE_DISPLAY_RUNTIME_CONTRACT.md`: fronteira guest/eventos/renderer, estado, texturas, draws, present, critérios de trace/teste.
- REA 6.8.0 executado; provider Ghidra bloqueado por `GHIDRA_INSTALL_DIR` ausente. Evidência em `C:\Users\Lucas\AppData\Local\Temp\opencode\rea-runtime\`.

## Falta

1. Configurar REA com Ghidra 12.1.4; probes nos handlers `0x140049c10`, `0x140049860`, `0x14004a450`.
2. Cruzar payload/ownership com trace Data East.
3. Não modificar `src/` sem gap comportamental demonstrado.
