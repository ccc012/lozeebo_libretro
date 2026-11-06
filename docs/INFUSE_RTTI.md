# Infuse RTTI Report

- Binary: `reference/infuse-analysis/Infuse.exe`
- Image base: `0x0000000140000000`
- RTTI descriptors reported: 41
- Total RTTI descriptors in binary: 620

## Method

Parsed the PE (pefile if available, else manual), located `.rdata`, collected
candidate Itanium/MSVC type-name strings, then found type descriptors whose
`+8` field points at such a string and whose `+0` vtable-pointer field points
into a valid section. For each descriptor, all references (slots) were located;
a slot is flagged as a *vtable candidate* when the preceding 8 bytes are zero
(offset-to-top) and the following 8 bytes point into `.text` (first virtual
function). Vtable base = slot + 8 (RVA).

## Classes

| # | Demangled class | Mangled name | Type descriptor RVA | Name RVA | Vptr RVA | Vtable candidates (base RVA) |
|---|---|---|---|---|---|---|
| 1 | `Infuse::Audio` | `N6Infuse5AudioE` | 0x5a5d10 | 0x5a94e0 | 0x5b7960 | 0x5b82b0 |
| 2 | `Infuse::AudioBackendWaveOut` | `N6Infuse19AudioBackendWaveOutE` | 0x5a5c00 | 0x5a92d0 | 0x5b7960 | 0x5b8030 |
| 3 | `Infuse::AudioObjectMIDI` | `N6Infuse15AudioObjectMIDIE` | 0x5a5b80 | 0x5a9230 | 0x5b7960 | 0x5b7ec0 |
| 4 | `Infuse::AudioObjectMP3` | `N6Infuse14AudioObjectMP3E` | 0x5a5b20 | 0x5a91d0 | 0x5b7960 | 0x5b7d50 |
| 5 | `Infuse::AudioObjectPCM` | `N6Infuse14AudioObjectPCME` | 0x5a5b40 | 0x5a91f0 | 0x5b7960 | 0x5b7df0 |
| 6 | `Infuse::AudioResampler` | `N6Infuse14AudioResamplerE` | 0x5a5b60 | 0x5a9210 | 0x5b7960 | 0x5b7e90 |
| 7 | `Infuse::Configuration` | `N6Infuse13ConfigurationE` | 0x5a5af0 | 0x5a9170 | 0x5b78d0 | 0x5b7d30 |
| 8 | `Infuse::ExecutionDynarec` | `N6Infuse16ExecutionDynarecE` | 0x5a5bb0 | 0x5a9270 | 0x5b7960 | 0x5b7f60 |
| 9 | `Infuse::FramerateCap` | `N6Infuse12FramerateCapE` | 0x5a5ad0 | 0x5a9130 | 0x5b78d0 | 0x5b7c70 |
| 10 | `Infuse::IAudio` | `N6Infuse6IAudioE` | 0x5a5d50 | 0x5a9500 | 0x5b78d0 | - |
| 11 | `Infuse::IAudioBackend` | `N6Infuse13IAudioBackendE` | 0x5a5b00 | 0x5a9190 | 0x5b78d0 | - |
| 12 | `Infuse::IAudioObject` | `N6Infuse12IAudioObjectE` | 0x5a5ae0 | 0x5a9150 | 0x5b78d0 | - |
| 13 | `Infuse::IAudioResampler` | `N6Infuse15IAudioResamplerE` | 0x5a5ba0 | 0x5a9250 | 0x5b78d0 | - |
| 14 | `Infuse::IExecution` | `N6Infuse10IExecutionE` | 0x5a5ac0 | 0x5a9110 | 0x5b78d0 | - |
| 15 | `Infuse::IInput` | `N6Infuse6IInputE` | 0x5a5d60 | 0x5a9520 | 0x5b78d0 | - |
| 16 | `Infuse::IInputBackend` | `N6Infuse13IInputBackendE` | 0x5a5b10 | 0x5a91b0 | 0x5b78d0 | - |
| 17 | `Infuse::IRenderer` | `N6Infuse9IRendererE` | 0x5a5ea0 | 0x5a97a0 | 0x5b78d0 | - |
| 18 | `Infuse::IRendererContext` | `N6Infuse16IRendererContextE` | 0x5a5bd0 | 0x5a9290 | 0x5b78d0 | - |
| 19 | `Infuse::Input` | `N6Infuse5InputE` | 0x5a5d30 | 0x5a94f0 | 0x5b7960 | 0x5b8390 |
| 20 | `Infuse::InputBackendGLFW` | `N6Infuse16InputBackendGLFWE` | 0x5a5be0 | 0x5a92b0 | 0x5b7960 | 0x5b7f90 |
| 21 | `Infuse::OpenGL::IRendererEvent` | `N6Infuse6OpenGL14IRendererEventE` | 0x5a5d70 | 0x5a9540 | 0x5b78d0 | - |
| 22 | `Infuse::OpenGL::Renderer` | `N6Infuse6OpenGL8RendererE` | 0x5a5e80 | 0x5a9780 | 0x5b7960 | 0x5b85c0 |
| 23 | `Infuse::OpenGL::RendererEventClear` | `N6Infuse6OpenGL18RendererEventClearE` | 0x5a5d80 | 0x5a9580 | 0x5b7960 | 0x5b8440 |
| 24 | `Infuse::OpenGL::RendererEventCreateTexture` | `N6Infuse6OpenGL26RendererEventCreateTextureE` | 0x5a5e00 | 0x5a9680 | 0x5b7960 | 0x5b8500 |
| 25 | `Infuse::OpenGL::RendererEventDeleteTexture` | `N6Infuse6OpenGL26RendererEventDeleteTextureE` | 0x5a5e20 | 0x5a96c0 | 0x5b7960 | 0x5b8530 |
| 26 | `Infuse::OpenGL::RendererEventDrawImage` | `N6Infuse6OpenGL22RendererEventDrawImageE` | 0x5a5de0 | 0x5a9640 | 0x5b7960 | 0x5b84d0 |
| 27 | `Infuse::OpenGL::RendererEventDrawText` | `N6Infuse6OpenGL21RendererEventDrawTextE` | 0x5a5da0 | 0x5a95c0 | 0x5b7960 | 0x5b8470 |
| 28 | `Infuse::OpenGL::RendererEventDrawTriangles` | `N6Infuse6OpenGL26RendererEventDrawTrianglesE` | 0x5a5e40 | 0x5a9700 | 0x5b7960 | 0x5b8560 |
| 29 | `Infuse::OpenGL::RendererEventUpdateTexture` | `N6Infuse6OpenGL26RendererEventUpdateTextureE` | 0x5a5e60 | 0x5a9740 | 0x5b7960 | 0x5b8590 |
| 30 | `Infuse::OpenGL::RendererEventViewport` | `N6Infuse6OpenGL21RendererEventViewportE` | 0x5a5dc0 | 0x5a9600 | 0x5b7960 | 0x5b84a0 |
| 31 | `Infuse::RendererContextGLFW` | `N6Infuse19RendererContextGLFWE` | 0x5a5c20 | 0x5a92f0 | 0x5b7960 | 0x5b8060 |
| 32 | `Infuse::UserInterfaceActivity` | `N6Infuse21UserInterfaceActivityE` | 0x5a5c40 | 0x5a9320 | 0x5b78d0 | - |
| 33 | `Infuse::UserInterfaceActivityAbout` | `N6Infuse26UserInterfaceActivityAboutE` | 0x5a5c70 | 0x5a93a0 | 0x5b7960 | 0x5b8170 |
| 34 | `Infuse::UserInterfaceActivityBackground` | `N6Infuse31UserInterfaceActivityBackgroundE` | 0x5a5cd0 | 0x5a9460 | 0x5b7960 | 0x5b8230 |
| 35 | `Infuse::UserInterfaceActivityExecution` | `N6Infuse30UserInterfaceActivityExecutionE` | 0x5a5cb0 | 0x5a9420 | 0x5b7960 | 0x5b81f0 |
| 36 | `Infuse::UserInterfaceActivityExecutionUnimplemented` | `N6Infuse43UserInterfaceActivityExecutionUnimplementedE` | 0x5a5cf0 | 0x5a94a0 | 0x5b7960 | 0x5b8270 |
| 37 | `Infuse::UserInterfaceActivityMain` | `N6Infuse25UserInterfaceActivityMainE` | 0x5a5c50 | 0x5a9360 | 0x5b7960 | 0x5b8130 |
| 38 | `Infuse::UserInterfaceActivitySettings` | `N6Infuse29UserInterfaceActivitySettingsE` | 0x5a5c90 | 0x5a93e0 | 0x5b7960 | 0x5b81b0 |
| 39 | `InfuseHAL` | `9InfuseHAL` | 0x5a5890 | 0x5a8d10 | 0x5b78d0 | - |
| 40 | `NativeInfuseHAL` | `15NativeInfuseHAL` | 0x5a5830 | 0x5a8cc0 | 0x5b7960 | 0x5b7560 |
| 41 | `std::thread::_State_impl<std::thread::_Invoker<std::tuple<void (Infuse::ExecutionDynarec::*)(unsigned int const&, unsigned int const&), Infuse::ExecutionDynarec*, unsigned int, unsigned int> > >` | `NSt6thread11_State_implINS_8_InvokerISt5tupleIJMN6Infuse16ExecutionDynarecEFvRKjS6_EPS4_jjEEEEEE` | 0x5a6920 | 0x5aa4e0 | 0x5b7960 | 0x5b9640 |

## Vtable candidate details

### `Infuse::Audio`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b82a8 | 0x5b82b0 | 0x4c430 | .rdata |

### `Infuse::AudioBackendWaveOut`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b8028 | 0x5b8030 | 0x1460 | .rdata |

### `Infuse::AudioObjectMIDI`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b7eb8 | 0x5b7ec0 | 0x50610 | .rdata |

### `Infuse::AudioObjectMP3`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b7d48 | 0x5b7d50 | 0x66e40 | .rdata |

### `Infuse::AudioObjectPCM`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b7de8 | 0x5b7df0 | 0x5b150 | .rdata |

### `Infuse::AudioResampler`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b7e88 | 0x5b7e90 | 0x4b450 | .rdata |

### `Infuse::Configuration`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b7d28 | 0x5b7d30 | 0x1e6f0 | .rdata |

### `Infuse::ExecutionDynarec`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b7f58 | 0x5b7f60 | 0x5e50 | .rdata |

### `Infuse::FramerateCap`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b7c68 | 0x5b7c70 | 0x695f0 | .rdata |

### `Infuse::Input`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b8388 | 0x5b8390 | 0x681e0 | .rdata |

### `Infuse::InputBackendGLFW`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b7f88 | 0x5b7f90 | 0x2d20 | .rdata |

### `Infuse::OpenGL::Renderer`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b85b8 | 0x5b85c0 | 0x3d6d0 | .rdata |

### `Infuse::OpenGL::RendererEventClear`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b8438 | 0x5b8440 | 0x494d0 | .rdata |

### `Infuse::OpenGL::RendererEventCreateTexture`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b84f8 | 0x5b8500 | 0x494e0 | .rdata |

### `Infuse::OpenGL::RendererEventDeleteTexture`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b8528 | 0x5b8530 | 0x494f0 | .rdata |

### `Infuse::OpenGL::RendererEventDrawImage`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b84c8 | 0x5b84d0 | 0x49c10 | .rdata |

### `Infuse::OpenGL::RendererEventDrawText`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b8468 | 0x5b8470 | 0x4a450 | .rdata |

### `Infuse::OpenGL::RendererEventDrawTriangles`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b8558 | 0x5b8560 | 0x49860 | .rdata |

### `Infuse::OpenGL::RendererEventUpdateTexture`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b8588 | 0x5b8590 | 0x49500 | .rdata |

### `Infuse::OpenGL::RendererEventViewport`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b8498 | 0x5b84a0 | 0x49510 | .rdata |

### `Infuse::RendererContextGLFW`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b8058 | 0x5b8060 | 0x4b50 | .rdata |

### `Infuse::UserInterfaceActivityAbout`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b8168 | 0x5b8170 | 0x73500 | .rdata |

### `Infuse::UserInterfaceActivityBackground`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b8228 | 0x5b8230 | 0x6ca60 | .rdata |

### `Infuse::UserInterfaceActivityExecution`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b81e8 | 0x5b81f0 | 0x763f0 | .rdata |

### `Infuse::UserInterfaceActivityExecutionUnimplemented`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b8268 | 0x5b8270 | 0x77460 | .rdata |

### `Infuse::UserInterfaceActivityMain`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b8128 | 0x5b8130 | 0x70b30 | .rdata |

### `Infuse::UserInterfaceActivitySettings`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b81a8 | 0x5b81b0 | 0x74500 | .rdata |

### `NativeInfuseHAL`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b7558 | 0x5b7560 | 0xaab0 | .rdata |

### `std::thread::_State_impl<std::thread::_Invoker<std::tuple<void (Infuse::ExecutionDynarec::*)(unsigned int const&, unsigned int const&), Infuse::ExecutionDynarec*, unsigned int, unsigned int> > >`

| Typeinfo slot RVA | Vtable base RVA | First vfunc RVA | Section |
|---|---|---|---|
| 0x5b9638 | 0x5b9640 | 0x428a30 | .rdata |

## All references (non-vtable)

- `Infuse::IAudio`: 0x5a5d20 (.rdata)
- `Infuse::IAudioBackend`: 0x5a5c10 (.rdata)
- `Infuse::IAudioObject`: 0x5a5b30 (.rdata), 0x5a5b50 (.rdata), 0x5a5b90 (.rdata), 0x5b7c88 (.rdata)
- `Infuse::IAudioResampler`: 0x5a5b70 (.rdata)
- `Infuse::IExecution`: 0x5a5bc0 (.rdata)
- `Infuse::IInput`: 0x5a5d40 (.rdata)
- `Infuse::IInputBackend`: 0x5a5bf0 (.rdata)
- `Infuse::IRenderer`: 0x5a5e90 (.rdata)
- `Infuse::IRendererContext`: 0x5a5c30 (.rdata)
- `Infuse::OpenGL::IRendererEvent`: 0x5a5d90 (.rdata), 0x5a5db0 (.rdata), 0x5a5dd0 (.rdata), 0x5a5df0 (.rdata), 0x5a5e10 (.rdata), 0x5a5e30 (.rdata), 0x5a5e50 (.rdata), 0x5a5e70 (.rdata)
- `Infuse::UserInterfaceActivity`: 0x5a5c60 (.rdata), 0x5a5c80 (.rdata), 0x5a5ca0 (.rdata), 0x5a5cc0 (.rdata), 0x5a5ce0 (.rdata), 0x5a5d00 (.rdata), 0x5b80e8 (.rdata)
- `InfuseHAL`: 0x5a5840 (.rdata)
