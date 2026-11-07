# Family Pack regression gate — FAIL loud (exit 1) on any break.
# Usage: powershell -File tools/fp-gate/run.ps1
param(
    [string]$BuildDir = "build-fp-gate",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $root

$baselinePath = Join-Path $PSScriptRoot "baseline.json"
if (-not (Test-Path $baselinePath)) {
    Write-Host "FAIL: missing baseline.json at $baselinePath" -ForegroundColor Red
    exit 1
}
$b = Get-Content -Raw $baselinePath | ConvertFrom-Json

function Fail([string]$msg) {
    Write-Host "FAIL: $msg" -ForegroundColor Red
    exit 1
}

# --- build Release if needed ---
$rel = Join-Path $root "$BuildDir\Release"
$smoke = Join-Path $rel "libretro_smoke.exe"
$dll = Join-Path $rel "lozeebo_libretro.dll"

if (-not $SkipBuild) {
    $needConfigure = -not (Test-Path (Join-Path $root "$BuildDir\CMakeCache.txt"))
    if ($needConfigure) {
        Write-Host "cmake configure $BuildDir (Release + smoke)..."
        & cmake -S $root -B (Join-Path $root $BuildDir) -DZEEBO_BUILD_SMOKE_TEST=ON
        if ($LASTEXITCODE -ne 0) { Fail "cmake configure failed (exit $LASTEXITCODE)" }
    }
    Write-Host "cmake build Release..."
    & cmake --build (Join-Path $root $BuildDir) --config Release
    if ($LASTEXITCODE -ne 0) { Fail "cmake build failed (exit $LASTEXITCODE)" }
}

if (-not (Test-Path $smoke)) { Fail "missing smoke exe: $smoke" }
if (-not (Test-Path $dll)) { Fail "missing core dll: $dll" }
if (-not (Test-Path $b.mod)) { Fail "missing MOD: $($b.mod)" }

# --- run smoke ---
$out = Join-Path $root "artifacts\fp-gate"
New-Item -ItemType Directory -Force $out | Out-Null
$ppm = Join-Path $out "fp-gate-900.ppm"
$log = Join-Path $out "fp-gate-900.log"
Remove-Item $ppm, $log -ErrorAction SilentlyContinue

$env:ZEEMU_SMOKE_MAX_SECONDS = "$($b.max_seconds)"
$env:ZEEMU_SMOKE_HOLD_FRAMES = "0"
$env:ZEEMU_SMOKE_SEQ = $b.seq

Write-Host "smoke $($b.frames)f seq=$($b.seq)"
# Native stderr via cmd — PS `2>` can hang/empty on this binary.
$cmdLine = "`"$smoke`" `"$dll`" `"$($b.mod)`" `"$ppm`" $($b.frames) 2>`"$log`""
cmd /c $cmdLine
$smokeExit = $LASTEXITCODE

$logText = ""
if (Test-Path $log) { $logText = Get-Content -Raw $log }

# --- criteria ---
$failures = @()

if ($smokeExit -ne 0) {
    $failures += "smoke exit=$smokeExit (expected 0)"
}

$timing = [regex]::Match($logText, '\[SMOKE-TIMING\]\s+frames=(\d+)\s+elapsed_ms=(\d+)')
$summary = [regex]::Match($logText, '\[SMOKE-SUMMARY\].*')

if (-not $timing.Success) {
    $failures += "missing [SMOKE-TIMING] in log"
    $elapsed = -1
    $framesDone = -1
} else {
    $framesDone = [int]$timing.Groups[1].Value
    $elapsed = [int64]$timing.Groups[2].Value
}

if (-not $summary.Success) {
    $failures += "missing [SMOKE-SUMMARY] in log"
    $videoVaried = -1
    $audioNonzero = -1
} else {
    $sumLine = $summary.Value
    $vm = [regex]::Match($sumLine, 'video_varied=(\d+)')
    $am = [regex]::Match($sumLine, 'audio_nonzero=(\d+)')
    $videoVaried = if ($vm.Success) { [int]$vm.Groups[1].Value } else { -1 }
    $audioNonzero = if ($am.Success) { [int64]$am.Groups[1].Value } else { -1 }
    if (-not $vm.Success) { $failures += "video_varied missing in SUMMARY" }
    if (-not $am.Success) { $failures += "audio_nonzero missing in SUMMARY" }
}

if ($videoVaried -ne [int]$b.video_varied) {
    $failures += "video_varied=$videoVaried (expected $($b.video_varied))"
}

if ($audioNonzero -lt [int64]$b.audio_nonzero_min) {
    $failures += "audio_nonzero=$audioNonzero < min $($b.audio_nonzero_min)"
}

if ($elapsed -gt [int64]$b.elapsed_ms_max) {
    $failures += "elapsed_ms=$elapsed > max $($b.elapsed_ms_max) (FPS regression; baseline $($b.elapsed_ms_baseline))"
}

# log poison patterns (case-insensitive substring)
$lower = $logText.ToLowerInvariant()
foreach ($pat in $b.fail_log_patterns) {
    $needle = ([string]$pat).ToLowerInvariant()
    if ($needle.Length -gt 0 -and $lower.Contains($needle)) {
        $failures += "log contains '$pat'"
    }
}

# PPM SHA256 strict
$ppmHash = $null
if (-not (Test-Path $ppm)) {
    $failures += "missing PPM: $ppm"
} else {
    $ppmHash = (Get-FileHash -Algorithm SHA256 -Path $ppm).Hash.ToUpperInvariant()
    $expect = ([string]$b.ppm_sha256).ToUpperInvariant()
    if ($ppmHash -ne $expect) {
        $failures += "PPM SHA256=$ppmHash != baseline $expect"
    }
}

# --- report ---
Write-Host ""
Write-Host "=== FP-GATE REPORT ==="
Write-Host "smoke_exit=$smokeExit frames=$framesDone elapsed_ms=$elapsed"
Write-Host "video_varied=$videoVaried audio_nonzero=$audioNonzero"
Write-Host "ppm_sha256=$ppmHash"
Write-Host "baseline_sha=$($b.ppm_sha256)"
Write-Host "log=$log"
Write-Host "ppm=$ppm"
if ($summary.Success) { Write-Host $summary.Value }
if ($timing.Success) { Write-Host $timing.Value }

if ($failures.Count -gt 0) {
    Write-Host ""
    Write-Host "GATE FAILED ($($failures.Count) criteria):" -ForegroundColor Red
    foreach ($f in $failures) { Write-Host "  - $f" -ForegroundColor Red }
    exit 1
}

Write-Host ""
Write-Host "GATE PASS" -ForegroundColor Green
exit 0
