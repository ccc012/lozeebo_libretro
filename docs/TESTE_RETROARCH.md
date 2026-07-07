# ?? Teste do Skeleton no RetroArch

## ?? Localização da DLL

Arquivo compilado:
```
C:\Users\Lucas\source\repos\zeebo_libretro\x64\Release\zeebo_libretro.dll
```

## ?? Instalação no RetroArch

### 1. Encontrar Pasta de Cores do RetroArch

**Windows - Opções Comuns:**
```
C:\RetroArch\cores\              (Instalação portátil)
C:\Program Files\RetroArch\cores\ (Instalação padrão)
%APPDATA%\RetroArch\cores\       (Instalação do usuário)
```

Para verificar, abra RetroArch:
? Settings ? Directories ? Core
Vá nessa pasta.

### 2. Copiar a DLL

```powershell
# PowerShell - Execute como Administrador

$dllSource = "C:\Users\Lucas\source\repos\zeebo_libretro\x64\Release\zeebo_libretro.dll"
$retroarchCores = "C:\RetroArch\cores\"  # Ajuste conforme necessário

Copy-Item -Path $dllSource -Destination $retroarchCores -Force
Write-Host "? DLL copiada para $retroarchCores"
```

Ou manualmente:
1. Abra `C:\Users\Lucas\source\repos\zeebo_libretro\x64\Release\`
2. Copie `zeebo_libretro.dll`
3. Cole em `C:\RetroArch\cores\`

## ?? Testes

### Teste 1: Verificar se o Core Aparece

1. Abrir **RetroArch**
2. Menu ? **Load Core** (ou Core)
3. Procurar "**Zeebo**" na lista
4. Se aparecer "Zeebo - 0.1-skeleton" ? ? SUCESSO

### Teste 2: Carregar um "Jogo" Fake

Como não temos ROMs reais ainda, vamos testar com um arquivo qualquer:

1. Criar um arquivo fake:
```powershell
# PowerShell
"fake content" | Out-File -FilePath "C:\tmp\test.mod" -Encoding ASCII
```

2. No RetroArch:
   - Load Core ? **Zeebo**
   - Load Content ? Selecionar `C:\tmp\test.mod`
   - Deve mostrar uma **tela preta** (sem crashes)

### Teste 3: Verificar Logs

Se houver logs, deverá aparecer:
```
[Zeebo] retro_init() chamado
[Zeebo] retro_get_system_info() chamado
[Zeebo] retro_load_game: C:\tmp\test.mod
[Zeebo] retro_run() executando...
```

Verifique:
- **Linux**: `~/.retroarch-pre.log` ou terminal
- **Mac**: `~/Library/Logs/RetroArch/retroarch.log`
- **Windows**: Abrir RetroArch do terminal para ver saída

## ?? Troubleshooting

### "Core não aparece na lista"
- [ ] Verificar se a DLL foi copiada para a pasta certa
- [ ] Verificar extensão (.dll no Windows)
- [ ] Reiniciar RetroArch
- [ ] Verificar se a DLL não está corrompida:
  ```powershell
  Get-Item "C:\RetroArch\cores\zeebo_libretro.dll" | Select-Object Length
  # Deve ter ~12 KB
  ```

### "Erro ao carregar core"
- [ ] Core pode ter dependências faltando (MSVC runtime)
  - Instale: https://support.microsoft.com/pt-br/help/2977003
- [ ] Verificar permissões de pasta
- [ ] Tentar recompilar com `msbuild`

### "Tela fica preta e trava"
- [ ] Isso é esperado no skeleton (não implementamos lógica real)
- [ ] Pressionar ESC para voltar ao menu

### "No error, mas RetroArch não carrega"
- [ ] Verificar se libretro.h tem as definições corretas
- [ ] Verificar se a compilação realmente criou a DLL:
  ```powershell
  Test-Path "C:\Users\Lucas\source\repos\zeebo_libretro\x64\Release\zeebo_libretro.dll"
  ```

## ? Critérios de Sucesso

- [x] DLL compilada sem erros
- [ ] DLL copiada para RetroArch
- [ ] "Zeebo" aparece em Load Core
- [ ] Core carrega sem crash
- [ ] Tela preta aparece

Quando todos os ? acima estiverem marcados, o skeleton está **100% funcional**!

## ?? Próximo Passo

Com o skeleton testado, começar a **Fase 1: CPU ARM**

? Ver `docs/PROGRESS.md` para próximas etapas

