# Ovos gate - criterios comprovados @ 9e21101 (2026-11-07).
# Pass: exit 0, video_varied=1, video_ever_varied=1, 0 invalid/STACK/descarril.
# Audio NAO e criterio (SetMediaData ok; IMedia_Play ainda 0 -> audio_nonzero=0).
# Sem fake HandleEvent(evt=2) neste CLSID 0x010963A5.
param(
    [int]$Frames = 180,
    [string]$Mod = "D:\Temp\zeebo_all\Um_Jogo_de_Ovos__Brazil___Es_Pt_\mod\279036\game.mod",
    [string]$Build = "build-ovos\Release",
    [string]$OutDir = "artifacts\ovos-gate"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $root

$smoke = Join-Path $root (Join-Path $Build "libretro_smoke.exe")
$core  = Join-Path $root (Join-Path $Build "lozeebo_libretro.dll")
if (-not (Test-Path $smoke)) { throw "missing $smoke - build with ZEEBO_BUILD_SMOKE_TEST=ON Release" }
if (-not (Test-Path $core))  { throw "missing $core" }
if (-not (Test-Path $Mod))   { throw "missing mod $Mod" }

$out = Join-Path $root $OutDir
New-Item -ItemType Directory -Force $out | Out-Null
$ppm = Join-Path $out ("f{0}.ppm" -f $Frames)
$log = Join-Path $out ("f{0}.log" -f $Frames)

Remove-Item Env:ZEEMU_SMOKE_SEQ -ErrorAction SilentlyContinue
Remove-Item Env:ZEEMU_SMOKE_BUTTON -ErrorAction SilentlyContinue
Remove-Item Env:ZEEMU_SMOKE_PRESS_FRAME -ErrorAction SilentlyContinue
Remove-Item Env:ZEEMU_SMOKE_DUMP_DIR -ErrorAction SilentlyContinue
$env:ZEEMU_SMOKE_HOLD_FRAMES = "0"
$env:ZEEMU_SMOKE_MAX_SECONDS = [string]([Math]::Max(120, [int]($Frames / 2) + 60))

$p = Start-Process -FilePath $smoke -ArgumentList @($core, $Mod, $ppm, "$Frames") `
    -NoNewWindow -Wait -PassThru -RedirectStandardError $log
$exit = $p.ExitCode

$summary = Select-String -Path $log -Pattern "\[SMOKE-SUMMARY\]" | Select-Object -Last 1
if (-not $summary) {
    Write-Host "FAIL: no SMOKE-SUMMARY (exit=$exit)"
    exit 1
}
$line = $summary.Line
Write-Host $line

$varied = if ($line -match "video_varied=(\d+)") { [int]$Matches[1] } else { -1 }
$ever   = if ($line -match "video_ever_varied=(\d+)") { [int]$Matches[1] } else { -1 }
$raw = Get-Content $log -Raw
$badInvalid = ([regex]::Matches($raw, "(?i)\binvalid\b")).Count
$badStack   = ([regex]::Matches($raw, "\bSTACK\b")).Count
$badDesc    = ([regex]::Matches($raw, "(?i)descarril")).Count

$fail = @()
if ($exit -ne 0) { $fail += "exit=$exit" }
if ($varied -ne 1) { $fail += "video_varied=$varied" }
if ($ever -ne 1) { $fail += "video_ever_varied=$ever" }
if ($badInvalid -gt 0) { $fail += "invalid=$badInvalid" }
if ($badStack -gt 0) { $fail += "STACK=$badStack" }
if ($badDesc -gt 0) { $fail += "descarril=$badDesc" }

if ($fail.Count -gt 0) {
    Write-Host ("FAIL: " + ($fail -join ", "))
    exit 1
}

$hash = (Get-FileHash $ppm -Algorithm SHA256).Hash
Write-Host "PASS exit=0 varied=1 ever=1 invalids=0 ppm=$hash"
if ($line -match "audio_nonzero=(\d+)") {
    Write-Host ("note: audio_nonzero=" + $Matches[1] + " (not gated; Play still absent)")
}
exit 0
