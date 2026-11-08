# Regressão de funcionalidades

```powershell
python tools/regression.py
```

Configura/compila Release e executa contratos de áudio ADPCM, transições de input,
rearme FP, direcionais DD, carregamento de pacotes completos/cache/segurança,
frontend archive/member, watchdog, referências existentes FP/DD/Ovos e testemunhas
visuais/áudio. Jogos são fixtures; não são a unidade de compatibilidade.

- `--skip-build`: usar build existente explicitamente; pode estar desatualizado.
- `--build-dir build-fp-gate`: selecionar build.
- `--contracts-only`: execução parcial, sem as 13 testemunhas visuais/áudio.
- `--self-test`: sucesso/falha/timeout do executor.
- Relatórios/logs em `artifacts/regression/<execução>/results.json`.
- Qualquer falha/timeout/arquivo ausente retorna código diferente de zero.
- ROMs locais e 7-Zip instalado necessários; arquivos comerciais não distribuídos.
- Baselines não são atualizados pelo executor.

Detalhes e limites das testemunhas: `tools/capability-gate/README.md`.
Imagem não uniforme não prova textura/palette correta, menu funcional ou gameplay.
Dez testemunhas têm hash histórico reproduzido; três só protegem saída não uniforme
(e áudio quando comprovado), sem cobertura snapshot reivindicada.
Gate Ovos protege inicialização, não gameplay. Direcionais DD protegem transporte,
não deslocamento visual. Referência FP de 900 frames permanece intacta.

Os testes existentes de pacotes aceitam `ZEEBO_TEST_BUILD_DIR`, definido pelo executor,
para testar a mesma DLL dos demais contratos em vez de um build antigo separado.
A execução completa depende das fixtures locais; não usar resultados parciais como
aprovação geral. Não executar duas suítes simultâneas: alguns gates antigos usam
nomes fixos nos próprios diretórios de artefatos.
