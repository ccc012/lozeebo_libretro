# Family Pack — seleção consistente

Base: `87da7dd`. Branch: `fix/fp-input-consistency`.
Pacote completo: `D:\Temp\zeebo_all\Zeebo_Family_Pack__Brazil___Es_Pt_\mod\277229\game.mod`.

## Resultado

Cannonball selecionado/carregado em **5/5** execuções independentes, com DOWN sustentado por 1, 2, 5, 10 e 30 frames; confirmação **RetroPad B**, não A. Nenhum ajuste de debounce ou timing aplicado ao runtime: a evidência não justifica alterar o contrato HID global.

Sequência: `DOWN:950:<hold>,B:1100:10`, 1350 frames. O alvo começa imediatamente à esquerda da seleção inicial; DOWN gira o carrossel até Cannonball. RIGHT não equivale a avançar esse carrossel. Screenshot intermediário `artifacts/fpdown.png` mostra Cannonball destacado. Cada execução abre `cannonball\gfx\bird.png`; screenshot após confirmar mostra ampulheta de carregamento. Isso comprova entrada no alvo, **não gameplay concluído**.

`A:1050:5` após RIGHT e `A:1100:10` após DOWN deixam o menu aberto; B carrega o alvo. Não trocar A/B globalmente: DD usa outro remapper e perderia compatibilidade.

## Timing observado

- `retro_run` consulta input uma vez por frame; relógio guest avança 16 ms por frame.
- FP recebe mudança de posição via callback `0x5690`, GetMin/GetMax/GetPositionState slots 11/12/10; botão via callback `0x57A4`.
- DD usa eventos de botão e remapper próprio; diferença de caminho confirmada no código, **latência DD versus FP não medida**.
- Posição só notifica mudança; centro é suprimido pelo workaround existente. Não removido sem reprodução segura do crash documentado.
- Pulso DOWN de um frame já alcança o alvo; 30 frames não causa seleção adicional neste cenário. Nenhuma evidência de drift ou necessidade de debounce aqui.
- Os pulsos iniciais em frames 240/320 ocorriam antes do menu; frame 400 ainda mostra splash. Não são prova de input perdido pelo menu.

## Reprodução

```powershell
cmake -S . -B build-input -DZEEBO_BUILD_SMOKE_TEST=ON
cmake --build build-input --config Release
.\tools\fp-visual-nav\check_selection.ps1
```

Saída validada: `PASS hold=1`, `2`, `5`, `10`, `30`, `PASS selection=5/5`.
Logs/PPMs: `artifacts/fp-selection/hold-*.log`, `hold-*.ppm` (ignorados por Git).

O teste verifica abertura de asset exclusivo do alvo, conclusão dos 1350 frames e ausência de diagnósticos invalid/STACK/descarril. Não usa hash de vídeo animado como identidade do minigame.

Nota: execuções anteriores desta sessão em Ovos não pertencem a esta validação. Nenhum resultado de Ovos foi usado como evidência de Family Pack.
