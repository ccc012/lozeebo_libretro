# Análise Técnica de Bloqueadores - Zeebo LibRetro

## 1. Bloqueadores Críticos (Afetam TODOS os jogos)

### A. Heap/Memory Initialization
**Status**: ✅ Implementado
- `src/memory/zmem.c`: Heap de 32MB + Stack de 4MB
- `src/memory/zheap.c`: Alocação com `zheap_alloc()`
- **Verificação**: Logs devem mostrar "boot: start" sem erros de memória

### B. Boot State Machine
**Status**: ✅ Implementado (6 estados)
```c
BOOT_IDLE → BOOT_MOD_LOAD → BOOT_CREATE_APPLET → 
BOOT_APP_START → BOOT_RUNNING → [TIMERS/SIGNALS]
```
- **Problema potencial**: Falha em qualquer transição = jogo trava
- **Logs esperados**: `boot: AEEMod_Load`, `boot: CreateInstance`, `boot: EVT_APP_START`

### C. IShell Real (CreateInstance)
**Status**: ⚠️  Parcialmente implementado
```c
static uint32_t real_create_instance(uint32_t clsid) {
    if (clsid == AEECLSID_FILEMGR_REAL) return zbrew_create_filemgr();
    if (clsid == AEECLSID_EGL_REAL) return zegl_create_interface();
    if (clsid == AEECLSID_GL_REAL) return zgl_create_interface();
    return make_stub_interface(clsid);  // ← Fallback genérico
}
```

**Problema**: Se jogo pede CLSID desconhecido:
- Retorna stub genérico (não necessariamente funcional)
- Pode falhar em vtable calls específicas

**Impacto Potencial**: 20-30% dos jogos podem ter CLSIDs customizados

---

## 2. Bloqueadores de Renderização

### A. glDrawArrays (GLFN_DrawArrays)
**Status**: ✅ Fixado (11 Julho)
- Antes: chamava `draw_test_quad()` (hardcoded)
- Agora: chama `draw_prim(mode, count, NULL, 0, 0)` com dados reais
- **Impacto**: Family Pack agora renderiza cores

### B. Vertex Array Setup
**Status**: ⚠️  Funcional mas incompleto
```c
case GLFN_VertexPointer:     // Posição XYZ
case GLFN_TexCoordPointer:   // Coordenadas UV
case GLFN_ColorPointer:      // Cor RGBA  ← PODE NÃO ESTAR IMPLEMENTADO
case GLFN_NormalPointer:     // Normais ← NÃO IMPLEMENTADO
```

**Risco**: Jogos que usam color/normal arrays podem renderizar errado

### C. Blend Modes / Depth Test
**Status**: ❌ Não implementado
```c
glBlendFunc(src, dst)       // ← IGNORADO
glDepthFunc(func)           // ← IGNORADO
glEnable(GL_DEPTH_TEST)     // ← IGNORADO
```

**Impacto**: Menus semi-transparentes renderizam opaco ou errado

---

## 3. Bloqueadores de Input/HID

### Status: ⚠️  Implementado mas não testado

**Fluxo atual**:
1. `zboot_tick()` checa `zinput_pressed()/zinput_released()`
2. Mapeia para evento HID (button id + uid + down)
3. Chama callback se signal registrado

**Problema**: 
- Signal callback pode não ser registrado antes de primeiro evento
- Button map pode estar incompleto (16 botões presentes?)

**Verificação**:
```c
// Esperado no logs:
LOGI("IHIDDevice_RegisterForButtonEvent signal=0x%08X", ...)
LOGI("HID: uid=0x%08X down=%u ...", ...)
```

---

## 4. Bloqueadores por Game-Specific CLSID

### A. Pac-Mania (0x01087B72)
**Problema em caso 5**: 
- ❌ Antes: Escrevia NULL nos out-params
- ✅ Agora: Aloca stubs reais via `make_stub_interface()`

### B. Family Pack (0x010903C6)
**Problema em glDrawArrays**:
- ❌ Antes: Draw test quad (branco)
- ✅ Agora: Draw com dados reais (colorido)

### C. Double Dragon (0x0102F789)
**Problema**: MOD raw format
- Entry point pode estar em offset 0
- Precisar validar se entry é válido (não zero, < RAM)

### D. Zeeboids (???...)
**Status**: CLSID desconhecido
- Logs devem mostrar qual CLSID é chamado em CreateInstance
- Se for CustomClass, será criado stub genérico

---

## 5. Checklist de Testes por Jogo

Para cada jogo testar em order:

```
[ ] 1. ROM carrega?
[ ] 2. Boot state machine progride (AEEMod_Load → OK)?
[ ] 3. CreateInstance retorna objeto válido?
[ ] 4. EVT_APP_START é chamado?
[ ] 5. Imagem renderiza (não tela branca)?
[ ] 6. Input responde?
```

---

## 6. Possíveis Fixes Rápidos (Próximas 30 min)

### Fix 1: MOD Raw Entry Point Validation
**Arquivo**: `src/loader/mod_loader.c`
**Mudança**: Validar se entry != 0 e entry < ZMEM_RAM_SIZE
**Impacto**: Double Dragon + raw MODs

### Fix 2: Stub Interface Defense
**Arquivo**: `src/brew/boot.c` (zbrew_handle_stub)
**Problema**: Alguns stubs podem retornar erros incorretos
**Mudança**: Adicionar mais case labels ou fallback default → 0 (sucesso)

### Fix 3: Input Signal Pre-Registration
**Arquivo**: `src/brew/boot.c` (zboot_start)
**Problema**: Signal pode não estar registrado antes de EVT_APP_START
**Mudança**: Pré-registrar signal durante setup_real_shell()

### Fix 4: Font Metrics Initialization
**Arquivo**: `src/brew/boot.c` (zbrew_handle_stub case 2)
**Status**: ✅ Já implementado
**Verificação**: Logs devem mostrar "GetFontMetrics" com valores 12/4/2/8

---

## 7. Métricas de Sucesso

| Métrica | Tier 1 (4 jogos) | Tier 2 (4 jogos) | Tier 3+ (60 jogos) |
|---|---|---|---|
| **Boot até "rodando"** | 4/4 | 3/4 | 20+/60 |
| **Renderização OK** | 2/4 | 1/4 | 10+/60 |
| **Input funciona** | 0/4 | 0/4 | 5+/60 |

---

## 8. Rastreamento de Logs

### Comandos úteis no RetroArch:
```bash
# Abrir logs
C:\Program Files (x86)\Steam\steamapps\common\RetroArch\logs

# Procurar por padrões
grep "\[Zeebo\]" *.log
grep "boot:" *.log
grep "CreateInstance\|glDrawArrays\|HID:" *.log
```

### Padrões esperados:
```
✅ [INFO] [Zeebo] boot: AEEMod_Load retornou 0x...
✅ [INFO] [Zeebo] boot: CreateInstance(0x...) via 0x...
✅ [INFO] [Zeebo] boot: EVT_APP_START tratado
✅ [INFO] [Zeebo] boot: jogo RODANDO

❌ [ERROR] [Zeebo] boot: AEEMod_Load falhou
❌ [ERROR] [Zeebo] boot: applet nao foi criado
```

---

## Próximos Steps

1. **Agora**: Lucas testa Tier 1 em RetroArch
2. **Durante testes**: Claude coleta logs e identifica CLSIDs
3. **Após 10 min**: Apply Fix 1 (MOD validation) se Double Dragon falhar
4. **Após 20 min**: Apply Fix 3 (Input pre-registration) se HID falhar
5. **Iteração**: Repete para Tier 2 e 3

