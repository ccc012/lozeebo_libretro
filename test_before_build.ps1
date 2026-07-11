# Pre-Build Tests - Valida codigo antes de compilar

$ErrorActionPreference = "Stop"
$failed = $false

Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host " PRE-BUILD TESTS" -ForegroundColor Cyan
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host ""

# Test 1: Verificar arquivos críticos existem
Write-Host "TEST 1: Verificando arquivos existem..." -ForegroundColor Yellow

$files = @(
    "src/brew/boot.c",
    "src/gpu/egl_gl.c",
    "src/memory/memory.c",
    "src/loader/mod_loader.c"
)

foreach ($file in $files) {
    if (Test-Path $file) {
        Write-Host "  OK: $file" -ForegroundColor Green
    } else {
        Write-Host "  ERRO: $file nao encontrado!" -ForegroundColor Red
        $failed = $true
    }
}

Write-Host ""

# Test 2: Verificar headers existem
Write-Host "TEST 2: Verificando headers..." -ForegroundColor Yellow

$headers = @(
    "src/gpu/egl_gl.h",
    "src/memory/memory.h",
    "src/cpu/cpu.h",
    "src/debug/log.h"
)

foreach ($header in $headers) {
    if (Test-Path $header) {
        Write-Host "  OK: $header" -ForegroundColor Green
    } else {
        Write-Host "  ERRO: $header nao encontrado!" -ForegroundColor Red
        $failed = $true
    }
}

Write-Host ""

# Test 3: Verificar funcoes criticas
Write-Host "TEST 3: Verificando funcoes criticas..." -ForegroundColor Yellow

$func_checks = @{
    "src/gpu/egl_gl.c" = @("decode_vertex_ptr", "transform_vertex", "draw_prim");
    "src/brew/boot.c" = @("make_stub_interface", "zbrew_handle_stub")
}

foreach ($file in $func_checks.Keys) {
    $content = Get-Content $file -Raw
    foreach ($func in $func_checks[$file]) {
        if ($content -match $func) {
            Write-Host "  OK: $func em $file" -ForegroundColor Green
        } else {
            Write-Host "  ERRO: $func nao encontrado em $file!" -ForegroundColor Red
            $failed = $true
        }
    }
}

Write-Host ""

# Test 4: Verificar fixes foram aplicadas
Write-Host "TEST 4: Verificando fixes aplicadas..." -ForegroundColor Yellow

# Fix 1: decode_vertex_ptr validation
$egl_content = Get-Content "src/gpu/egl_gl.c" -Raw
if ($egl_content -match "decode_vertex_ptr" -and $egl_content -match "alt.*0x04000000") {
    Write-Host "  OK: decode_vertex_ptr tem validacao" -ForegroundColor Green
} else {
    Write-Host "  AVISO: decode_vertex_ptr pode estar incompleto" -ForegroundColor Yellow
}

# Fix 2: Pac-Mania case 5 validation
$boot_content = Get-Content "src/brew/boot.c" -Raw
if ($boot_content -match "case 5" -and $boot_content -match "0x0100101C") {
    Write-Host "  OK: Pac-Mania case 5 implementado" -ForegroundColor Green
} else {
    Write-Host "  ERRO: Pac-Mania case 5 nao encontrado!" -ForegroundColor Red
    $failed = $true
}

Write-Host ""
Write-Host "=====================================================" -ForegroundColor Cyan

if ($failed) {
    Write-Host " TESTE FALHOU - Nao compilar!" -ForegroundColor Red
    exit 1
} else {
    Write-Host " TESTES OK - Pronto para compilar!" -ForegroundColor Green
    exit 0
}
