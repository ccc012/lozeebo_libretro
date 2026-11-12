# 🔄 Workflow de Desenvolvimento Seguro

**Objetivo**: Testar código ANTES de compilar para evitar erros

---

## 📋 Checklist: Qualquer Mudança no Código

### ANTES de mexer no código:
```powershell
cd C:\Users\Lucas\source\repos\zeebo_libretro
```

### 1️⃣ **Faça a mudança** (edite o arquivo)
   - Exemplo: `src/gpu/egl_gl.c` ou `src/brew/boot.c`

### 2️⃣ **Execute os testes** (obrigatório!)
```powershell
.\test_before_build.ps1
```

**Resultado esperado:**
```
====================================================
 PRE-BUILD TESTS
====================================================
...
TEST 4: Verificando fixes aplicadas...
  OK: decode_vertex_ptr tem validacao
  OK: Pac-Mania case 5 implementado
=====================================================
 TESTES OK - Pronto para compilar!
```

**Se falhar:**
```
 TESTE FALHOU - Nao compilar!
```
→ Volte e corrija o código

### 3️⃣ **Compile com build seguro**
```powershell
.\build_safe.ps1
```

**Fluxo automático:**
1. Roda `test_before_build.ps1`
2. Se passar → compila
3. Se compilar OK → instala em RetroArch

**Se quiser pular testes (⚠️ só em emergências):**
```powershell
.\build_safe.ps1 -SkipTests
```

### 4️⃣ **Teste em RetroArch**
- Abra RetroArch
- Load Core → Zeebo
- Load Content → jogo
- Observa resultado

### 5️⃣ **Se funcionou**: Pronto! ✅
### Se não funcionou: Volta ao passo 1️⃣

---

## 🛡️ Testes Automáticos Inclusos

```
TEST 1: Arquivos críticos existem
TEST 2: Headers estão presentes
TEST 3: Funções críticas foram implementadas
TEST 4: Fixes foram aplicadas corretamente
```

---

## 📝 Exemplo Prático: Implementar Uma Nova Fix

### Cenário: Bug em glDrawArrays

**Passo 1: Identifique o arquivo**
```
src/gpu/egl_gl.c → linhas 1157-1200
```

**Passo 2: Faça a mudança**
```c
case GLFN_DrawArrays: {
    // Sua fix aqui
    uint32_t mode = a0;
    int count = (int)a2;
    // ...
}
```

**Passo 3: Testes
```powershell
.\test_before_build.ps1
```

**Esperado:**
```
TEST 3: Verificando funcoes criticas...
  OK: draw_prim em src/gpu/egl_gl.c ← seu arquivo está OK
```

**Passo 4: Compile**
```powershell
.\build_safe.ps1
```

**Esperado:**
```
✅ Compilação OK em 5.2s
✅ DLL instalada em RetroArch
```

**Passo 5: Teste**
- Abra RetroArch → Family Pack
- Observa se a fix funcionou

---

## 🚨 Erros Comuns & Como Evitar

### ❌ Erro: "TESTE FALHOU"
**Causa**: Arquivo C tem sintaxe errada ou função falta

**Solução**:
1. Procura por `ERRO:` na saída do teste
2. Volta e corrige o arquivo mencionado
3. Roda `test_before_build.ps1` novamente

### ❌ Erro: "Compilação falhou"
**Causa**: Mesmo com testes passando, compilador encontrou erro

**Solução**:
1. Lê mensagem do MSBuild
2. Procura a linha de erro no arquivo
3. Corrige (geralmente parênteses ou tipo de dado)
4. Roda `build_safe.ps1` novamente

### ❌ Erro: "DLL não renderiza"
**Causa**: Código compilou mas tem lógica errada

**Solução**:
1. Coleta logs do RetroArch
2. Procura por mensagens `[Zeebo]` ou `ERRO`
3. Identifica qual função falhou
4. Volta ao passo 1 do workflow

---

## 🔗 Arquivos do Workflow

| Arquivo | O que faz |
|---------|-----------|
| `test_before_build.ps1` | Testa código antes de compilar |
| `build_safe.ps1` | Compila com testes automáticos |
| `WORKFLOW_DESENVOLVIMENTO.md` | Este arquivo (guia) |

---

## ⚡ Resumo: Um Commit = Um Workflow Completo

```
1. Edit file (ex: src/gpu/egl_gl.c)
   ↓
2. .\test_before_build.ps1
   ↓ (se OK)
3. .\build_safe.ps1
   ↓ (se OK)
4. Testa em RetroArch
   ↓ (se OK)
5. Pronto para próximo jogo!
```

**Tempo total**: ~30 segundos (testes) + 10 segundos (compilação) + tempo de teste em RetroArch

---

## 📊 Status Atual

✅ Todos os testes passando
✅ Código pronto para compilar
✅ 2 fixes aplicadas (decode_vertex_ptr + Pac-Mania case 5)
✅ DLL instalada em RetroArch

**Próximo**: Execute os testes com mudanças reais nos jogos!

