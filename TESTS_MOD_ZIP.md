# TESTS_MOD_ZIP

## Simulação teórica do loader

### 1. MOD é detectado como ARM válido?

- Sim, o loader suporta duas formas:
  - header BREW com magic `BREW`
  - binário ARM cru, com entry no offset 0 ou branch inicial detectado
- Em `zmod_load()`, se houver header BREW, o loader calcula `entry` pelo branch do início.
- Se não houver header, tenta tratar como binário cru.

### 2. ZIP será rejeitado?

- Sim.
- O loader rejeita conteúdo começando com `PK` seguido de `0x03`, `0x05` ou `0x07`.
- Isso impede o core de tentar executar um container comprimido como código ARM.

### 3. GZIP/GGZ será rejeitado?

- Sim.
- O loader verifica `0x1F 0x8B` e aborta com mensagem clara.

## Simulação do boot flow

### Sequência esperada

`AEEMod_Load -> IModule_CreateInstance -> EVT_APP_START -> RUNNING`

### Onde cada hipótese falha

- Hipótese 1: falha em `CreateInstance`
  - Não foi o caso do `Zeeboids` no smoke atual.
  - O smoke já passou por `AEEMod_Load` e foi para `PC=0x0048EE74`.

- Hipótese 2: chega a `rodando` mas `halted=true`
  - Isso acontece no desenho normal do boot depois de `EVT_APP_START`.
  - Porém, no `Zeeboids` atual, o problema principal observado foi descarrilamento, não apenas espera passiva.

- Hipótese 3: renderiza 1 frame e depois trava
  - Há vídeo produzido no smoke.
  - O primeiro frame aparece, mas o jogo descarrila logo após.

### Ponto atual do `Zeeboids`

- O jogo não está preso no loader ZIP.
- O jogo não está preso em `CreateInstance` neste momento.
- O bloqueio atual está no boot/runtime do guest, com fetch inválido em `0x04000000`.

## Conclusão

- O teste teórico confirma que o loader faz a triagem básica corretamente.
- O gargalo de hoje não é compressão nem falta de entry point.
- O maior suspeito para o próximo passo é o estado de boot/applet e a cadeia que leva ao descarrilamento após `AEEMod_Load`.

