# Infuse.exe — Análise de Interfaces e Contratos (pt-BR)

> Decompilação Ghidra 12.1.4 headless do binário `Infuse.exe` (Tuxality, emulador Zeebo de referência).
> Objetivo: documentar ordem de slots de vtable, assinaturas, máquinas de estado e contratos dos stubs DISPLAY/GRAPHICS para o core LOZeebo (retomada RE4 — tela preta 3D).

---

## 1. Sumário de Vtables Encontradas

| Vtable RVA | Classe (RTTI) | Slots | Uso |
|---|---|---|---|
| `0x1405b7560` | `NativeInfuseHAL` | 78 | HAL principal — BREW API surface |
| `0x1405b7df0` | `AudioObjectPCM` | 17 | Áudio PCM |
| `0x1405b7e90` | `AudioResampler` | 4 | Reamostragem |
| `0x1405b7ec0` | `AudioObjectMIDI` | 17 | Áudio MIDI |
| `0x1405b7f60` | `ExecutionDynarec` | 4 | JIT/interprete |
| `0x1405b7f90` | `FramerateCap` | 11 | Limitador de FPS |
| `0x1405b8030` | `AudioBackendWaveOut` | 4 | Backend áudio |
| `0x1405b8060` | `RendererContextGLFW` | 15 | Contexto OpenGL |
| `0x1405b8130` | `UserInterfaceActivityMain` | 6 | UI |
| `0x1405b8170` | `UserInterfaceActivityAbout` | 6 | UI |
| `0x1405b81b0` | `UserInterfaceActivitySettings` | 6 | UI |
| `0x1405b81f0` | `UserInterfaceActivityExecution` | 6 | UI |
| `0x1405b8230` | `UserInterfaceActivityBackground` | 6 | UI |
| `0x1405b8270` | `UserInterfaceActivityExecutionUnimpl` | 6 | UI |
| `0x1405b82b0` | `Infuse::Audio` | 11 | Wrapper áudio |
| `0x1405b8390` | `Input` | 4 | Entrada |
| `0x1405b85c0` | `OpenGL::Renderer` | 17 | Renderizador OpenGL |

---

## 2. IShell (NativeInfuseHAL) — Vtable 78 slots @ `0x1405b7560`

É a vtable maior e contém a superfície API BREW. Abaixo a ordem real dos slots com as funções decompiladas.
