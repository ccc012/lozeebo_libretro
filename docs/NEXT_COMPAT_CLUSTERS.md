# Próximos clusters de compatibilidade

## Escopo e método

Análise estática dos scripts/código e comparação dos sweeps de 180 frames (`sweep_pass3.csv`, `sweep_current_180.csv`, `sweep_after_pool.csv`, `sweep_after_common.csv`). Excluídos Resident Evil 4, Pac-Mania, Zeebo Family Pack e Super BurgerTime. Nenhum arquivo em `src/` foi alterado.

O harness classifica como `video uniforme` quando `video_varied=0` e como `video sem audio` quando há variação sem áudio (`sweep_68_games.ps1:53-64`). Portanto, hash uniforme não prova ausência de execução; as assinaturas abaixo combinam hash, último erro e repetição entre sweeps.

## Prioridade 1 — loop assíncrono sem trabalho (timer/sinal perdido)

**Tamanho observado:** 13 jogos no `sweep_after_common.csv`; recorrente, com pequenas oscilações, nos demais sweeps.

**Jogos:** Action Hero 3D, Alien Breaker Deluxe, Alpine Racer, Crash Bandicoot Nitro Kart 3D, Disney All Star Cards, Double Dragon, FIFA 09, Heavy Weapon, Need for Speed Carbon, Tork and Kral, Zeebo App, Zeebo Extreme Baja, Zeebo Extreme Corrida Aérea, Zeebo Sports Vôlei e Zenonia. O conjunto exato varia entre builds; isto é parte da assinatura, não evidência de correções isoladas.

**Assinatura:**

- quadro uniforme, frequentemente hash `A6087EC5...`;
- áudio zerado;
- `TIMERPROBE: state=falhou now=1936 halted=1 active=0 first.exp=0 first.pfn=0x00000000`;
- em algumas execuções o mesmo jogo termina sem erro, mas continua uniforme.

**Causa provável:** a máquina de boot chega a um estado terminal/abortado sem timer, sinal ou thread capaz de retomá-la. `zboot_process_timers()` só processa a fila em `BOOT_RUNNING` (`src/brew/boot.c:1224-1227`), agenda no máximo um callback (`1248-1263`) e mantém a CPU parada sem trabalho (`1266-1268`). O código contém re-registro ad hoc do game loop (`1125-1145`), indicando que a ausência de agendamento já é conhecida, mas a política não cobre todos os caminhos de retorno/erro. A variação entre sweeps sugere ordem de callbacks/estado compartilhado, não defeitos independentes por jogo.

**Correção transversal proposta:** centralizar a conclusão de toda chamada guest (applet, timer, sinal e thread) numa única transição que: (1) restaure `BOOT_RUNNING`; (2) preserve timers registrados pelo guest; (3) reprograme somente o callback principal quando o applet continua ativo e a fila está vazia; (4) nunca converta falha transitória em estado `falhou` silencioso. Remover exceções por título/engine após validar a política geral.

**Teste:**

1. Rodar os jogos acima por 600 frames com `ZEEMU_TRACE_TIMER=1` e a sequência padrão de `sweep_68_games.ps1`.
2. Falhar se, após `EVT_APP_START`, ocorrerem 120 frames consecutivos com `halted=1`, zero timers, zero sinais e applet ativo.
3. Exigir pelo menos dois callbacks do loop principal e `video_varied=1` em três execuções consecutivas de Alpine Racer, NFS Carbon e Zenonia.
4. Regressão: executar também um jogo que registra timer próprio; não pode surgir timer duplicado nem crescimento contínuo da fila.

## Prioridade 2 — módulos ARMCC crus entram no bootstrap como se fosse callback

**Tamanho observado:** 8 jogos, 7 após a exclusão de Super BurgerTime.

**Jogos:** Bad Dudes vs. DragonNinja, Heavy Barrel, Caveman Ninja, Spin Master, Street Hoop, Dark Seal, Karnov's Revenge e Magical Drop III.

**Assinatura:**

- quadro compartilhado `0C98A08D...`, com variação mas sem áudio;
- queda extremamente estável em `fetch em 0xE24F4020`;
- pares de LR por família: `0xF2C4` (Bad Dudes/Heavy Barrel), `0x11014` (Caveman Ninja/Spin Master/Street Hoop), `0x1100C` (Dark Seal/Karnov/Magical Drop);
- no sweep anterior à mudança do pool, a mesma família aparecia como `chamada nula (PC=0)` nos mesmos LRs.

**Causa provável:** `0xE24F4020` é uma instrução ARM do prólogo ARMCC, não um endereço de função. O loader reconhece exatamente essa assinatura em módulos crus (`src/loader/mod_loader.c:181-194`). A execução posterior trata uma palavra de código/prológo como ponteiro, compatível com base PIC, relocação, entry real ou tabela de imports calculada incorretamente. O agrupamento por LR mostra uma única runtime compartilhada entre vários ports Data East.

**Correção transversal proposta:** modelar corretamente o bootstrap ARMCC cru: resolver e validar o entry real, aplicar a base PIC/relocações uma vez, materializar a tabela de imports esperada e rejeitar qualquer callback cujo alvo seja uma palavra ARM lida como endereço. Não criar desvios por jogo; a assinatura do loader deve selecionar um único caminho de carga para toda a família.

**Teste:**

1. Adicionar teste de loader com os oito `.mod`: entry alinhado, dentro da imagem executável, diferente de `0xE24F4020` e de zero.
2. Rodar 600 frames; falhar em fetch para qualquer palavra de instrução do bootstrap (`0xE24F4020`, `0xE92D...`, `0xE1A0...`).
3. Exigir passagem além dos LRs conhecidos e frame hash diferente de `0C98A08D...` em Bad Dudes, Caveman Ninja e Dark Seal.
4. Comparar os oito logs: o mesmo resolvedor/import map deve ser usado, sem regras por ID.

## Prioridade 3 — ponteiros de função recebem opcodes ARM

**Tamanho observado:** ao menos 5 jogos em duas subassinaturas repetíveis.

**Jogos:** Bejeweled Twist, Zuma's Revenge e Zenonia (`0xE92D400C`); Zeebo F.C. Foot Camp e Zeeboids (`0xE1A0F002`). Raging Thunder 2 apresenta variante próxima (`0xE59AC004`/`0xE1A0E000`) e deve integrar a amostra de diagnóstico.

**Assinatura:**

- `CPU descarrilou: fetch em 0xE92D400C` para Bejeweled/Zuma/Zenonia;
- `CPU descarrilou: fetch em 0xE1A0F002` para Foot Camp/Zeeboids;
- os destinos são opcodes ARM plausíveis (`push`/movimento para PC), não VAs válidas;
- vídeo uniforme `A6087EC5...` na maioria das execuções.

**Causa provável:** confusão comum entre endereço e conteúdo: leitura/dereferência extra (ou falta dela) em vtable, callback, import thunk ou relocação. O padrão é distinto do cluster ARMCC: aqui diferentes engines saltam para a primeira instrução armazenada no alvo. Stubs genéricos amplificam o risco: uma vtable única de 128 slots é usada para CLSIDs desconhecidos (`src/brew/boot.c:574-599`), apesar de interfaces terem layouts e convenções diferentes. O próprio comentário do display documenta que layout incorreto faz lixo virar função e causa salto inválido (`566-573`).

**Correção transversal proposta:** introduzir validação única antes de toda chamada indireta guest: alvo alinhado, mapeado e executável; detectar alvo com valor igual à palavra lida no endereço esperado e registrar origem (`obj`, `vtbl`, slot, CLSID, LR). Em seguida, corrigir o nível de indireção no dispatcher/import resolver e substituir stubs genéricos apenas para os CLSIDs/slots revelados pelo trace por vtables tipadas. A validação diagnostica; não deve mascarar o erro retornando sucesso.

**Teste:**

1. Executar Bejeweled, Zuma, Zeeboids e Foot Camp com trace de chamadas indiretas até a primeira falha.
2. Assertar que nenhum PC recebe diretamente um opcode lido de vtable/import (`0xE92D400C`, `0xE1A0F002`, `0xE59AC004`).
3. O log deve identificar objeto, CLSID, slot e LR para qualquer alvo inválido.
4. Após a correção, exigir 600 frames sem fetch inválido e pelo menos um frame diferente do hash uniforme; repetir três vezes para detectar dependência de ordem.

## Ordem recomendada

1. Corrigir a política de conclusão/agendamento: maior alcance, desbloqueia diagnóstico posterior.
2. Corrigir o caminho único dos módulos ARMCC crus: cluster mais homogêneo, teste de alto sinal.
3. Instrumentar e corrigir indireção/vtables: cluster menor, provável impacto transversal adicional.

## Evidência principal

- `sweep_after_common.csv:2-62`
- `sweep_after_pool.csv:2-62`
- `sweep_current_180.csv:2-62`
- `sweep_pass3.csv:2-62`
- `sweep_68_games.ps1:53-64`
- `src/brew/boot.c:566-599, 1125-1268`
- `src/loader/mod_loader.c:181-194`
