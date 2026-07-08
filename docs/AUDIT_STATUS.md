# Auditoria Técnica Atual

## O que já está visivelmente implementado

- `src/core/libretro_core.c` integra core, CPU, memória, BREW, loader, input, vídeo e áudio.
- `src/cpu/` já contém interpretador ARM/Thumb com flags e barrel shifter.
- `src/memory/` já trata regiões separadas, leitura/escrita little-endian e bounds checking.
- `src/brew/` já faz dispatch HLE por endereços mágicos e cria interfaces emulada.
- `src/loader/` já carrega ROMs flat, tenta extrair nome do MIF, procura CLSIDs conhecidos e agora tem fallback por nome de caminho para Pac-Mania/Double Dragon.

## O que ainda precisa de validação

- Build nesta máquina ainda não passou por causa do SDK do Windows no ambiente.
- `tests/test_cpu.c` e `tests/test_memory.c` ainda estão vazios.
- A compatibilidade com jogos reais ainda depende de validação prática, especialmente no fluxo do Pac-Man e de outros títulos Zeebo.

## Observação de licença

- A documentação foi alinhada para **GPLv3**.
- O projeto pode incorporar referências, adaptações ou trechos de outros projetos, desde que a origem e a licença sejam registradas em `docs/THIRD_PARTY.md`.

## Próxima prioridade

1. Validar build em um ambiente que não bloqueie o SDK do Windows.
2. Rodar o core no RetroArch.
3. Fechar a auditoria dos módulos com comportamento de runtime.
## Atualizacao de runtime - Pac-Mania

- O nucleo compila em `Release|x64` com Visual Studio 2022.
- `tests/libretro_smoke.c` executa ROMs e captura logs sem depender da
  interface grafica do RetroArch.
- O Pac-Mania passa por `AEEMod_Load`, resolve o CLSID `0x01087B72` e entra em
  `IModule_CreateInstance`.
- A classe BREW `0x0100101C` e criada como stub e seus dois resultados do
  metodo 5 agora sao inicializados.
- Bloqueio atual: durante a saida de `CreateInstance`, o SP avanca para a
  regiao de VRAM e a CPU termina em `0xEA00000C`.
# Zeebo Family Pack - validacao real

- CLSID `0x010903C6` extraido do MIF real e registrado como Zeebo Family Pack.
- O applet agora passa por `AEEMod_Load`, `IModule_CreateInstance`, inicializacao de display, arquivos, som e joystick.
- `AEEHelper_GetAppInstance` foi implementado com base no comportamento do Zeemu.
- `SignalCBFactory_CreateSignal` agora cria e devolve objetos validos nos dois parametros de saida.
- `IHID_GetConnectedDevices`, `GetDeviceInfo` e `CreateDevice` expoem um joystick Zeebo valido.
- `IFileMgr` real foi conectado ao boot; o jogo ja tenta abrir `udata\highscore.dat`.
- `IDisplay_GetDeviceBitmap` devolve um bitmap RGB565 de 640x480 associado a VRAM.
- `CreateInstance` termina com applet valido e `EVT_APP_START=0` e tratado; o estado chega a `rodando` sem descarrilar.
- Bloqueio atual: o Family Pack cria `AEECLSID_EGL` e `AEECLSID_GL`, mas essas interfaces ainda sao stubs. O applet inicia sem produzir frames; a proxima etapa e portar a pilha EGL/OpenGL ES GPLv3 do Zeemu e conecta-la ao framebuffer LibRetro.
