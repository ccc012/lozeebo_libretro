# Build rápido para testes paralelos

$sln = "zeebo_libretro.sln"
$config = "Release"
$platform = "x64"
$cores_dir = "C:\Program Files (x86)\Steam\steamapps\common\RetroArch\cores"

Write-Host "═════════════════════════════════════════" -ForegroundColor Cyan
Write-Host "  ZEEBO BUILD RÁPIDO" -ForegroundColor Cyan
Write-Host "═════════════════════════════════════════" -ForegroundColor Cyan
Write-Host ""

# Verificar se solução existe
if (-not (Test-Path $sln)) {
    Write-Host "❌ Solução não encontrada: $sln" -ForegroundColor Red
    exit 1
}

Write-Host "📦 Compilando $platform/$config..."
$start = Get-Date

# Build
msbuild $sln /p:Configuration=$config /p:Platform=$platform /m:4 /nologo /v:minimal

$elapsed = (Get-Date) - $start
$success = $LASTEXITCODE -eq 0

if (-not $success) {
    Write-Host ""
    Write-Host "❌ Build FALHOU em $($elapsed.TotalSeconds)s" -ForegroundColor Red
    exit 1
}

Write-Host "✅ Build OK em $([Math]::Round($elapsed.TotalSeconds, 1))s" -ForegroundColor Green

# Copiar para RetroArch
$dll = "$platform\$config\zeebo_libretro.dll"
if (Test-Path $dll) {
    $size = (Get-Item $dll).Length
    Write-Host ""
    Write-Host "📥 Instalando DLL ($size bytes)..."

    Copy-Item $dll "$cores_dir\zeebo_libretro.dll" -Force

    if (Test-Path "$cores_dir\zeebo_libretro.dll") {
        Write-Host "✅ DLL instalada com sucesso" -ForegroundColor Green
        Write-Host "   Pronto para testar em RetroArch!"
    } else {
        Write-Host "❌ Falha ao copiar DLL" -ForegroundColor Red
        exit 1
    }
} else {
    Write-Host "❌ DLL não encontrada: $dll" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "═════════════════════════════════════════" -ForegroundColor Green
Write-Host "  ✅ PRONTO PARA TESTES" -ForegroundColor Green
Write-Host "═════════════════════════════════════════" -ForegroundColor Green
