# ═════════════════════════════════════════════════════════════════
# BUILD SEGURO - Testa antes de compilar
# ═════════════════════════════════════════════════════════════════

param(
    [switch]$SkipTests
)

Write-Host "═════════════════════════════════════════════════════════════════" -ForegroundColor Cyan
Write-Host " 🔍 BUILD SEGURO - Com Pré-Testes" -ForegroundColor Cyan
Write-Host "═════════════════════════════════════════════════════════════════" -ForegroundColor Cyan
Write-Host ""

# ─────────────────────────────────────────────────────────────────
# PASSO 1: Testes de Pré-Build (se não skip)
# ─────────────────────────────────────────────────────────────────
if (-not $SkipTests) {
    Write-Host "PASSO 1: Executando testes de pré-build..." -ForegroundColor Cyan
    Write-Host ""

    & .\test_before_build.ps1 -Full

    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "❌ Testes FALHARAM! Não compilando." -ForegroundColor Red
        exit 1
    }

    Write-Host ""
    Write-Host "✅ Testes passaram! Prosseguindo com compilação..." -ForegroundColor Green
    Write-Host ""
} else {
    Write-Host "⚠️  PULANDO TESTES (--SkipTests)" -ForegroundColor Yellow
    Write-Host ""
}

# ─────────────────────────────────────────────────────────────────
# PASSO 2: Compilar
# ─────────────────────────────────────────────────────────────────
Write-Host "PASSO 2: Compilando projeto..." -ForegroundColor Cyan
Write-Host ""

$msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
if (-not (Test-Path $msbuild)) {
    Write-Host "❌ MSBuild não encontrado em: $msbuild" -ForegroundColor Red
    exit 1
}

$start = Get-Date
& $msbuild zeebo_libretro.sln /p:Configuration=Release /p:Platform=x64 /m:4 /nologo /v:minimal

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "❌ COMPILAÇÃO FALHOU!" -ForegroundColor Red
    exit 1
}

$elapsed = (Get-Date) - $start
Write-Host ""
Write-Host "✅ Compilação OK em $([Math]::Round($elapsed.TotalSeconds, 1))s" -ForegroundColor Green

# ─────────────────────────────────────────────────────────────────
# PASSO 3: Verificar DLL
# ─────────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "PASSO 3: Verificando DLL..." -ForegroundColor Cyan
Write-Host ""

$dll = "x64\Release\zeebo_libretro.dll"
if (-not (Test-Path $dll)) {
    Write-Host "❌ DLL não criada!" -ForegroundColor Red
    exit 1
}

$dll_info = Get-Item $dll
Write-Host "✓ DLL criada: $($dll_info.Name)" -ForegroundColor Green
Write-Host "  Tamanho: $($dll_info.Length) bytes" -ForegroundColor Green
Write-Host "  Data: $($dll_info.LastWriteTime)" -ForegroundColor Green

# ─────────────────────────────────────────────────────────────────
# PASSO 4: Instalar em RetroArch
# ─────────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "PASSO 4: Instalando em RetroArch..." -ForegroundColor Cyan
Write-Host ""

$cores_dir = "C:\Program Files (x86)\Steam\steamapps\common\RetroArch\cores"
if (-not (Test-Path $cores_dir)) {
    Write-Host "⚠️  RetroArch cores folder não encontrado: $cores_dir" -ForegroundColor Yellow
    Write-Host "DLL pode ser copiado manualmente." -ForegroundColor Yellow
} else {
    Copy-Item $dll "$cores_dir\zeebo_libretro.dll" -Force
    Write-Host "✅ DLL instalada em RetroArch" -ForegroundColor Green

    $installed = Get-Item "$cores_dir\zeebo_libretro.dll"
    Write-Host "  Localização: $($installed.FullName)" -ForegroundColor Green
    Write-Host "  Tamanho: $($installed.Length) bytes" -ForegroundColor Green
}

# ─────────────────────────────────────────────────────────────────
# RESUMO FINAL
# ─────────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "═════════════════════════════════════════════════════════════════" -ForegroundColor Green
Write-Host " ✅ BUILD COMPLETO COM SUCESSO!" -ForegroundColor Green
Write-Host "═════════════════════════════════════════════════════════════════" -ForegroundColor Green
Write-Host ""
Write-Host "Próximos passos:" -ForegroundColor Cyan
Write-Host "1. Abra RetroArch" -ForegroundColor Cyan
Write-Host "2. Load Core → Zeebo" -ForegroundColor Cyan
Write-Host "3. Load Content → Jogo" -ForegroundColor Cyan
Write-Host "4. Observe se renderiza com imagem" -ForegroundColor Cyan
Write-Host ""
