# ?? INSTRUÇÕES DE TESTE - BUILD AVANÇADO

## ?? O Que Testar

Você agora tem uma versão **muito mais completa** do emulador Zeebo:

- ? CPU ARM com suporte a Thumb
- ? Memória com heap
- ? Carregador de ROMs (MOD/MIF/BAR)
- ? APIs BREW para HLE
- ? GPU e Áudio
- ? Input handling
- ? Debug tools

---

## ?? TESTE 1: VERIFICAR NO RETROARCH

### Passo 1: Copiar DLL

```powershell
# Execute no PowerShell como Administrador:
Copy-Item -Path "C:\Users\Lucas\source\repos\zeebo_libretro\x64\Release\zeebo_libretro.dll" `
          -Destination "C:\RetroArch\cores\" -Force
```

### Passo 2: Abrir RetroArch

```
Executável: RetroArch.exe
Menu: Main Menu
```

### Passo 3: Carregar Core

```
Main Menu
  ? Load Core
     ? Buscar "Zeebo"
        ? Deve aparecer "Zeebo" com versão
```

**Resultado Esperado**: ? "Zeebo - 0.1-skeleton" aparece na lista

---

## ?? TESTE 2: CARREGAR UM ARQUIVO

### Criar Arquivo Fake

```powershell
# Se não tiver arquivo MOD real, crie um fake:
"FAKE MOD CONTENT" | Out-File -FilePath "C:\tmp\test.mod" -Encoding ASCII
```

### Carregar no RetroArch

```
Após selecionar core "Zeebo":
  ? Load Content
     ? Selecionar "C:\tmp\test.mod"
        ? Pressionar Enter
```

**Resultado Esperado**:
- [ ] Tela preta aparece
- [ ] Sem crash
- [ ] RetroArch continua responsivo
- [ ] Pode ver logs de debug

---

## ?? TESTE 3: VER LOGS DE DEBUG

### Se Core Compilou com Debug Symbols

```
1. Abrir RetroArch do terminal:
   RetroArch.exe --verbose

2. Pressionar ESC para voltar ao menu
3. Ver stderr para mensagens de debug
```

### Se Usando Log File

```
1. Verificar arquivo de log do RetroArch:
   %APPDATA%\RetroArch\logs\

2. Procurar por "[Zeebo]" nos logs
3. Ver mensagens de inicialização
```

---

## ?? O QUE PODE ACONTECER

### Cenário 1: Core Aparece e Funciona ?
```
Resultado: SUCESSO!
Próximo: Testar com arquivos MOD reais
```

### Cenário 2: Core Aparece mas Trava ??
```
Motivo possível: Bug na implementação
Ação: Ver logs de debug
      Revisar código em src/
      Corrigir e recompilar
```

### Cenário 3: Core Não Aparece ?
```
Motivo possível: 
  - DLL não copiada corretamente
  - Permissões de pasta
  - Versão incompatível

Ação: Verificar C:\RetroArch\cores\
      Confirmar zeebo_libretro.dll existe
      Reiniciar RetroArch
```

---

## ?? TROUBLESHOOTING

### "Core não aparece no menu"

```
1. Verificar se DLL foi copiada:
   dir C:\RetroArch\cores\zeebo_libretro.dll

2. Se não existe, copiar manualmente

3. Verificar permissões:
   - Pasta C:\RetroArch\cores\ deve ser acessível
   - Usuário deve ter permissão de leitura

4. Reiniciar RetroArch
```

### "Erro ao carregar core"

```
Motivo: Pode estar faltando dependências

Solução:
  1. Instalar Visual C++ Runtime:
     https://support.microsoft.com/pt-br/help/2977003

  2. Se usar Windows 7:
     - Atualizar Windows
     - Instalar Service Packs
```

### "Tela fica preta e trava"

```
Motivo: Bug na implementação (esperado em fase de desenvolvimento)

Ação:
  1. Pressionar ESC para voltar ao menu

  2. Verificar logs:
     - Ver se há mensagens de erro
     - Procurar por stack traces

  3. Desabilitar core:
     - Remover DLL
     - Recompilar com correções
```

### "Preciso ver mais detalhes"

```
1. Habilitar logging avançado:
   RetroArch ? Settings ? Logging ? Verbose = ON

2. Ver logs em:
   ~/.retroarch/retroarch.log (Linux/Mac)
   C:\Users\[user]\AppData\Roaming\RetroArch\logs\ (Windows)

3. Procurar por "[Zeebo]" nos logs
```

---

## ?? TESTE 4: VERIFICAR PERFORMANCE

Se conseguir carregar um jogo:

```
1. Verificar FPS:
   Menu ? Information ? Framerate

2. Procurar por lag ou stuttering:
   - Jogo deve rodar a 60 FPS
   - Sem queda de frames

3. Verificar uso de CPU:
   - Abrir Task Manager
   - Procurar por RetroArch.exe
   - CPU deve estar abaixo de 50% (single core)
```

---

## ?? RELATÓRIO DE TESTE

Se testar, por favor gere um relatório:

```
Core: Zeebo 0.5 (Avançado)
Data: [data de teste]
Resultado: [SUCESSO / FALHA]

O que Funcionou:
- [ ] Core aparece no menu
- [ ] Carrega arquivo .mod
- [ ] Tela preta aparece
- [ ] Sem crash
- [ ] Logs aparecem

O que Não Funcionou:
- [ ] [Descrever problema]
- [ ] [Descrever problema]

Erros Observados:
- [Descrever]

Próximos Passos:
- [Sugerir]
```

---

## ?? PRÓXIMAS FASES

Se Teste 1-3 funcionarem:

1. **Teste com ROMs Reais**
   - Procurar por arquivos MOD legítimos
   - Testar carregamento
   - Validar execução

2. **Validar Instruções ARM**
   - Desassemblador deve mostrar instruções corretas
   - Flags devem mudar corretamente
   - Stack deve manter integridade

3. **Teste de Performance**
   - Medir velocidade de execução
   - Verificar consumo de memória
   - Otimizar código quente

4. **Teste de Compatibilidade**
   - Testar várias ROMs
   - Procurar por incompatibilidades
   - Documentar descobertas

---

## ?? SUPORTE

Se algo não funcionar:

1. Verificar `docs/COMPILACAO_AVANCADA.md`
2. Ver `docs/BUILD_AVANCADO_RESUMO.md`
3. Revisar `README.md`
4. Recompilar com debugging ativado

---

**Boa sorte no teste! ??**

Se tudo funcionar, é um passo GRANDE no desenvolvimento do emulador!

