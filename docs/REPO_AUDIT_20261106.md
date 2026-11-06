# Auditoria documental do repositório — 2026-11-06

## Escopo

Auditoria somente de documentação, metadados Git/GitHub e regras de higiene. Código-fonte, binários, ROMs, builds e worktrees de jogos ficaram fora do escopo.

## Estado encontrado

- Licença: `LICENSE` contém o texto integral da GPL-3.0.
- Metadados GitHub: `.github/` inexistente antes desta auditoria.
- README: completo, porém seu status principal retrata 2026-07-11 e diverge do snapshot de 2026-11-06.
- Documentação: 30 arquivos em `docs/` e 27 documentos históricos/operacionais na raiz.
- Coordenação: `agent-coord/` contém regras e frentes; a ficha `fronts/repo-hygiene.md` estava ausente.
- `.gitignore`: base Visual Studio extensa; cobre referências locais e builds conhecidos, mas não todos os diretórios `build-*` usados pelas frentes.

## Inventário e classificação

### Canônicos atuais

- `README.md`: entrada pública; deve apontar ao estado mais recente sem apagar o marco histórico.
- `docs/REPO_AUDIT_20261106.md`: esta auditoria e registro prévio de futuras remoções.
- `docs/INDEX.md`: mapa documental por finalidade e época.
- `docs/THIRD_PARTY.md`: licenças e referências externas.
- `docs/TESTING.md`: procedimentos de teste.
- `docs/NEXT_COMPAT_CLUSTERS.md`: próximos bloqueios transversais.
- `docs/AUDIO_REGRESSION_69_20261106.md`: evidência atual de áudio.

### Históricos preservados

- `docs/PLANNING_ARCHIVE.md`, `docs/PROGRESS.md`, `docs/ROADMAP_50_IDEIAS.md`.
- Relatórios focais `DDRAGON_*`, `PACMANIA_*`, `SUPBTIME_*`, `FAMILYPACK_*`, `RE4_*`, `ZEEBOIDS_*`.
- Relatórios de regressão datados e notas de engenharia reversa.
- Documentos de rodadas, prompts, briefings e status na raiz.

Esses arquivos registram decisões e evidências de épocas diferentes. Permanecem no lugar para evitar quebra de links e perda de proveniência.

### Sobreposição identificada

- `PACMANIA_AUDIO.md` e `PACMANIA_AUDIO2.md`: evolução sequencial, não cópias descartáveis.
- `SUPBTIME_VIDEO.md` e `SUPBTIME_RENDER.md`: focos relacionados, evidências distintas.
- `RE_INFUSE_NOTES.md`, `RETOMADA_REA_INFUSE.md`, `INFUSE_REVERSE_ENGINEERING.md`, `INFUSE_RTTI.md` e contratos `REA_INFUSE_*`: trilha histórica para contratos mais recentes.
- `PROGRESS.md`, `TRIAGE_ALL_GAMES.md`, `BLOCKERS_ANALYSIS.md` e `NEXT_COMPAT_CLUSTERS.md`: visões temporal, por jogo, por bloqueio e transversal.
- Prompts/status da raiz: artefatos operacionais históricos; não representam documentação pública atual.

## Ações desta frente

1. Criar índice documental, mantendo todos os arquivos e seus caminhos.
2. Marcar claramente no README o marco de julho como histórico e apontar ao snapshot de novembro.
3. Adicionar templates GitHub mínimos para issues e pull requests.
4. Cobrir builds locais nomeados com `build-*/` no `.gitignore`.
5. Registrar coordenação e handoff desta frente.

## Registro de remoções/movimentações

Nenhuma remoção ou movimentação realizada nesta auditoria.

Qualquer limpeza futura deve ser registrada aqui antes da alteração, incluindo caminho, motivo, substituto e commit de origem. Preferência: índice, marcador histórico ou arquivo em `docs/archive/`; exclusão somente para conteúdo gerado, comprovadamente duplicado ou legalmente impróprio.

## Riscos pendentes

- Muitos documentos históricos permanecem na raiz. Mover todos agora quebraria links e aumentaria o diff; arquivamento deve ocorrer em PR separado, com mapa de redirecionamento.
- O estado público ainda depende de consolidar um resumo versionado no repositório; o snapshot externo de 2026-11-06 não foi copiado integralmente para evitar duplicação divergente.
- Workflows de CI não foram adicionados: validar comandos/builds primeiro, depois automatizar sem prometer plataformas não verificadas.
