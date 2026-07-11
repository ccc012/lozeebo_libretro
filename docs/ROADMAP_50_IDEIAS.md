# Roadmap — 50 Ideias de Recursos (backlog categorizado)

> Lista trazida pelo Lucas em 2026-07-11, organizada por categoria e priorizada. Nenhum
> jogo roda de ponta a ponta ainda (ver [`README.md`](../README.md) e
> `STATUS_*.md`/`RODADA_*_CONSOLIDADO.md` na raiz do repo pro estado real) — por isso a
> esmagadora maioria destes itens é **backlog explícito**, não trabalho atual. Implementar
> "upscaling" ou "save states" antes de 1 jogo rodar seria desperdício de esforço:
> não há gameplay pra melhorar ainda.
>
> Convenção de prioridade:
> - 🔧 **Fazer logo** — ajuda o trabalho de diagnóstico *agora*, antes de qualquer jogo
>   estar 100% jogável.
> - 📌 **Backlog** — só faz sentido depois que pelo menos 1 jogo estiver realmente
>   jogável do início ao fim.

---

## 🔧 Ferramentas de Debug (fazer logo — grupo prioritário)

Estas 5 vieram da seção "Ferramentas de Desenvolvimento e Debug Avançado" da lista
original (itens #46-50) e são as únicas que aceleram diretamente as tarefas de hoje
(diagnosticar por que Pac-Mania/Zeeboids/Family Pack/Double Dragon ainda não rodam).
Detalhamento de implementação na "Tarefa Bônus" de `PROMPT_RODADA_3_ONBOARDING.md`.

| # orig. | Item | Por que agora |
|---|---|---|
| 46 | Visualizador de logs em tempo real / log estruturado por categoria | Filtrar `[Zeebo:BOOT]` vs `[Zeebo:GPU]` vs `[Zeebo:CPU]` acelera achar a causa de cada travamento |
| 47 | Inspetor de VRAM / visualizador de sprites | Ver o framebuffer sem depender de abrir o RetroArch a cada teste |
| 48 | Breakpoints por endereço de memória | Já teria acelerado o diagnóstico do bug de stack do Pac-Mania (SP→VRAM) |
| 49 | Contador de ciclos de instruções por região | Ajuda a achar loops degenerados como o do Zeeboids (CPU "andando" por RAM não inicializada) |
| 50 | Modo tolerante a crashes (só para diagnóstico exploratório, nunca padrão) | Deixa ver "até onde daria pra ir" sem parar no primeiro erro — mas nunca deve virar comportamento padrão silencioso, o projeto documenta deliberadamente "não finge que funciona" |

---

## 📌 Backlog — Vídeo (itens #1-10 da lista original)

Fazem sentido só depois que a rasterização básica (triângulos texturizados corretos, sem
vértices fora de tela) estiver 100% resolvida — hoje ainda é o bloqueio central do
Family Pack.

1. Upscaling interno de resolução (720p/1080p em vez de 480i nativo)
2. Filtro de textura bilinear/anisotrópico
3. Forçar aspect ratio (4:3 original vs. 16:9 widescreen)
4. Filtro CRT embutido no núcleo (sem depender de shaders do RetroArch)
5. Limitador de framerate customizável (30 vs. 60 FPS)
6. Paletas de cores alternativas (inclui acessibilidade/daltonismo)
7. Controle de brilho/contraste/saturação
8. Modo rotacionado (Tate) para jogos verticais
9. Exibição de FPS nativo do Zeebo (separado do contador do RetroArch)
10. Overclock da CPU ARM virtual

## 📌 Backlog — Controles e Periféricos (itens #11-20)

Fazem sentido depois que o input básico (RetroPad → bitmask Zeebo) estiver validado com
pelo menos 1 jogo jogável — hoje o input já existe mas nunca foi testado de ponta a
ponta (ver `BLOCKERS_ANALYSIS.md`, seção 3).

11. Mapeamento do Z-Pad clássico (perfil pronto pra controles modernos)
12. Simulação do Boomerang (giroscópio do controle de movimento do Zeebo)
13. Emulação do teclado Zeebo via teclado físico do PC
14. Deadzone customizável nos analógicos
15. Modo Rapid-Fire (turbo)
16. Troca de porta de controle "on-the-fly"
17. Suporte a mouse emulado (interface de cursor)
18. Hotkeys específicas do núcleo (reset da CPU emulada, etc.)
19. Simulação de desconexão de controle
20. Mapeamento de toque na tela (RetroArch mobile/Android)

## 📌 Backlog — Sistema de Arquivos, Memória e Saves (itens #21-30)

Save states dependem de serializar CPU+memória — tecnicamente possível a qualquer
momento, mas sem sentido prático até existir um estado de jogo "salvável" de verdade
(nenhum jogo chega no gameplay ainda). Já está no roadmap ativo do projeto como item
adiado deliberadamente ("fase 7").

21. Save states (serialização de CPU + RAM)
22. Navegador de arquivos interno (VFS) pra `.mod`/`.mif`
23. Gerenciador de "memória flash" virtual (progresso por jogo)
24. Dump de memória RAM pra `.bin` (útil pra quem faz tradução/hacks)
25. Compactação automática de saves
26. Carregamento direto de pastas soltas (sem empacotar)
27. Hard reset limpo (zera registradores e RAM de verdade)
28. Proteção contra escrita na ROM/código
29. Formato de save portável entre Windows/Linux/Android
30. Autosave periódico

## 📌 Backlog — Rede e "Zeebo Club" (itens #31-40)

Baixa prioridade — a rede do Zeebo original dependia de servidores que não existem mais;
tudo aqui é sobre simular respostas falsas de rede/loja pra não travar jogos que tentam
conectar. Só vale a pena depois que o boot básico dos jogos-alvo estiver resolvido.

31. Simulação de sinal 3G da "Z-Network" (stub sempre-sucesso)
32. Loja offline simulada (Zeebo Club apontando pra pasta local)
33. Simulador de autenticação de conta
34. Injeção de créditos virtuais (Z-Credits)
35. RTC sincronizado com o relógio real do PC
36. Leaderboards locais (captura o que o jogo tentaria mandar pra internet)
37. Desativador de telas de erro de conectividade infinitas
38. Emulação de download de DLCs
39. Stub de mensagens do sistema/operadora
40. Modo de rede desconectada nativa (força o jogo a entrar offline direto)

## 📌 Backlog — Áudio (itens #41-45)

Depende do mixer PCM básico (já funcional) evoluir pra suportar os formatos que os
jogos-alvo realmente usam — hoje só PCM cru, sem ADPCM/MP3/MIDI (ver roadmap em
`README.md`).

41. Sintetizador MIDI de alta qualidade
42. Controle de volume individual (música vs. efeitos)
43. Resampler de taxa de amostragem mais robusto (22kHz/44kHz → 48kHz sem estalo)
44. Dump de áudio (sound test mode, exporta `.wav`)
45. Suporte a áudio estéreo expandido

---

## Como usar este documento

Antes de puxar um item daqui pra implementar, confira o estado atual do projeto (ver
`STATUS_*.md`/`RODADA_*_CONSOLIDADO.md` mais recente na raiz do repo, ou
`PROMPT_RODADA_3_ONBOARDING.md`). Regra simples: **nenhum item deste backlog (fora do
grupo 🔧 Fazer logo) deve ser puxado antes de pelo menos 1 jogo do Tier 1 rodar
completamente jogável, do boot ao gameplay.** Antes disso, todo esforço de
desenvolvimento vai pros bloqueios de boot/renderização documentados nos `STATUS_*.md`.
