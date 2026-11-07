# Compare a complete extracted package with the unchanged FP reference.
param(
    [Parameter(Mandatory=$true)][string]$Mod,
    [string]$BuildDir = 'build-fp-gate'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Mod = (Resolve-Path -LiteralPath $Mod).Path
$baseline = Get-Content -Raw (Join-Path $PSScriptRoot 'baseline.json') | ConvertFrom-Json
$out = Join-Path $root 'artifacts\fp-content-path'
New-Item -ItemType Directory -Force $out | Out-Null
$ppm = Join-Path $out 'final.ppm'
$log = Join-Path $out 'smoke.log'
$oldSeq = $env:ZEEMU_SMOKE_SEQ
$oldMaxSeconds = $env:ZEEMU_SMOKE_MAX_SECONDS
$oldHoldFrames = $env:ZEEMU_SMOKE_HOLD_FRAMES
try {
    $env:ZEEMU_SMOKE_SEQ = 'A:240:10,A:300:10,RIGHT:420:120,LEFT:570:120,A:720:60'
    $smoke = Join-Path $root "$BuildDir\Release\libretro_smoke.exe"
    $dll = Join-Path $root "$BuildDir\Release\lozeebo_libretro.dll"
    $env:ZEEMU_SMOKE_MAX_SECONDS = '120'
    $env:ZEEMU_SMOKE_HOLD_FRAMES = '0'
    $commandLine = "`"$smoke`" `"$dll`" `"$Mod`" `"$ppm`" 900 2>`"$log`""
    cmd /c $commandLine
    if ($LASTEXITCODE -ne 0) { throw "Smoke failed: $LASTEXITCODE; $log" }
    $hash = (Get-FileHash -LiteralPath $ppm -Algorithm SHA256).Hash
    $expected = $baseline.ppm_sha256
    if (-not $expected) { throw 'Baseline ppm_sha256 missing' }
    if ($hash -ne $expected) { throw "Content differs: $hash != $expected; $log" }
    $text = Get-Content -Raw $log
    if ($text -notmatch 'audio_nonzero=931855\b') { throw "Audio reference mismatch; $log" }
    Write-Host "PASS complete package matches FP video/audio reference: $Mod"
} finally {
    $env:ZEEMU_SMOKE_SEQ = $oldSeq
    $env:ZEEMU_SMOKE_MAX_SECONDS = $oldMaxSeconds
    $env:ZEEMU_SMOKE_HOLD_FRAMES = $oldHoldFrames
}
