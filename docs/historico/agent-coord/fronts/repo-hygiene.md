# Frente: repo-hygiene

**Objetivo:** Organizar repositório e GitHub sem perder histórico.  
**Estado:** active  
**Branch/WT:** `front/repo-hygiene` @ `D:\zeebo-worktrees\repo-hygiene`  
**Owner:** documentação raiz, `.gitignore`, README, GitHub metadata/workflows; **não src/**  
**Remote:** `origin https://github.com/ccc012/lozeebo_libretro.git`

## Escopo

1. Inventariar documentação: atual, histórica, duplicada, contraditória
2. Melhorar README/índice/docs sem inventar compatibilidade
3. Organizar `.github/` (templates/CI simples) se útil
4. Melhorar `.gitignore` para artefatos; não esconder fonte necessária
5. Detectar binários/ROMs/segredos indevidos no histórico atual
6. Preparar commits pequenos na branch e publicar `front/repo-hygiene`
7. Produzir plano de arquivamento/remoção; não apagar histórico sem mestre
8. Abrir PR se `gh` autenticado; nunca mergear em `master`

## Restrições

- Não tocar `src/`, DLL RetroArch, worktrees de jogos
- Não `reset --hard`, `clean -fd`, force-push
- Não remover documentos históricos diretamente; mover para `docs/archive/` somente após inventário, em commit separado
- Não commitar ROMs, PPM, logs, dumps, builds
- Não alterar resultados técnicos; usar `09_RESUMO_ATUAL_2026-11-06.md` como fonte
- Push apenas da própria branch; mestre revisa e integra

## Entrega

- `docs/REPO_AUDIT_20261106.md`
- Commits organizados
- Handoff com arquivos removidos/movidos/propostos
- URL da branch/PR se publicada
