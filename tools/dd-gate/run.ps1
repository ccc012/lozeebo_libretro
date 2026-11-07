# Double Dragon regression gate — FAIL loud (exit 1).
# Usage: powershell -File tools/dd-gate/run.ps1 [-Frames 600] [-Seq "..."]
param(
    [int]$Frames = 600,
    [string]$Mod = "D:\Temp\zeebo_all\Double_Dragon__Brazil___Es_Pt_\mod\274754\ddragonz.mod",
    [string]$BuildDir = "build-dd-gate",
    [string]$Seq = "",
    [switch]$SkipBuild
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $root

function Fail([string]$msg) {
    Write-Host "FAIL: $msg" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $Mod)) {
    $fallback = Join-Path $root "tests\roms\real_ddragon_game.mod"
    if (Test-Path $fallback) { $Mod = $fallback }
    else { Fail "MOD ausente: $Mod" }
}

$rel = Join-Path $root "$BuildDir\Release"
$smoke = Join-Path $rel "libretro_smoke.exe"
$dll = Join-Path $rel "lozeebo_libretro.dll"

if (-not $SkipBuild) {
    $needConfigure = -not (Test-Path (Join-Path $root "$BuildDir\CMakeCache.txt"))
    if ($needConfigure) {
        Write-Host "cmake configure $BuildDir..."
        & cmake -S $root -B (Join-Path $root $BuildDir) -DZEEBO_BUILD_SMOKE_TEST=ON
        if ($LASTEXITCODE -ne 0) { Fail "cmake configure failed" }
    }
    Write-Host "cmake build Release..."
    & cmake --build (Join-Path $root $BuildDir) --config Release
    if ($LASTEXITCODE -ne 0) { Fail "cmake build failed" }
}

if (-not (Test-Path $smoke)) { Fail "smoke ausente: $smoke" }
if (-not (Test-Path $dll)) { Fail "dll ausente: $dll" }

$out = Join-Path $root "artifacts\dd-gate"
New-Item -ItemType Directory -Force $out | Out-Null
$tag = if ($Seq) { "seq-{0}" -f $Frames } else { "{0}" -f $Frames }
$ppm = Join-Path $out ("dd-{0}.ppm" -f $tag)
$log = Join-Path $out ("dd-{0}.log" -f $tag)
Remove-Item $ppm, $log -ErrorAction SilentlyContinue

$env:ZEEMU_SMOKE_MAX_SECONDS = [string]([Math]::Max(120, [int]($Frames / 2)))
Remove-Item Env:ZEEMU_SMOKE_DUMP_DIR -ErrorAction SilentlyContinue
if ($Seq) {
    $env:ZEEMU_SMOKE_SEQ = $Seq
    $env:ZEEMU_SMOKE_HOLD_FRAMES = "0"
} else {
    Remove-Item Env:ZEEMU_SMOKE_SEQ -ErrorAction SilentlyContinue
    Remove-Item Env:ZEEMU_SMOKE_HOLD_FRAMES -ErrorAction SilentlyContinue
}

Write-Host "smoke ${Frames}f mod=$Mod seq=$Seq"
$cmdLine = "`"$smoke`" `"$dll`" `"$Mod`" `"$ppm`" $Frames 2>`"$log`""
cmd /c $cmdLine
$code = $LASTEXITCODE
if ($code -ne 0) { Fail "exit $code (esperado 0)" }

$text = if (Test-Path $log) { Get-Content -Raw $log } else { "" }
if ($text -notmatch "\[SMOKE-SUMMARY\].*?video_varied=1") {
    Fail "video_varied!=1 ou SUMMARY ausente"
}
if ($text -notmatch "audio_nonzero=([1-9]\d*)") {
    Fail "audio_nonzero=0 ou ausente"
}
$sumLines = @(Select-String -Path $log -Pattern "SMOKE-SUMMARY|SMOKE-TIMING")
$sum = ($sumLines | ForEach-Object { $_.Line.Trim() }) -join " "

$gfx = ([regex]::Matches($text, "real_create_instance: GRAPHICS")).Count
if ($gfx -gt 2) { Fail "GRAPHICS CreateInstance=$gfx (retry loop; fake evt=2?)" }
$hidRel = ([regex]::Matches($text, "IHID Release:")).Count
if ($hidRel -gt 0) { Fail "IHID Release=$hidRel (fake evt=2 cancelou HID)" }

$data = ([regex]::Matches($text, "OpenFile: 'data\.ggz'")).Count
if ($data -lt 1) { Fail "data.ggz nao aberto (load nao avancou)" }

$hard = ([regex]::Matches($text, "(?i)descarril|STACK overflow|FATAL|INVALID opcode")).Count
if ($hard -gt 0) { Fail "falhas hard=$hard no log" }

$draw = ([regex]::Matches($text, "DrawArrays\[")).Count
if ($draw -lt 1) { Fail "DrawArrays ausente (sem cena GL pos-load)" }
$texOk = ([regex]::Matches($text, "CompressedTex:.*-> OK")).Count
if ($texOk -lt 1) { Fail "CompressedTex OK ausente" }

if ($Seq) {
    if ($text -notmatch "hid-button") {
        Fail "SEQ setada mas hid-button ausente no log"
    }
    # Pos-START: PPM nao pode ser o title idle conhecido
    $idleTitle = "2149E446D5DC3A50D2348E052C516C5CE0F133CEA26C7072A92117672E7E518A"
    $hash = (Get-FileHash $ppm -Algorithm SHA256).Hash
    if ($hash -eq $idleTitle) {
        Fail "PPM ainda e title idle apos SEQ (START nao avancou)"
    }
} else {
    $hash = (Get-FileHash $ppm -Algorithm SHA256).Hash
}

Write-Host "PASS frames=$Frames exit=0 varied=1 audio_nz ok data.ggz=$data GRAPHICS=$gfx DrawArrays=$draw"
Write-Host "SUMMARY $sum"
Write-Host "PPM_SHA256 $hash"
Write-Host "LOG $log"
Write-Host "PPM $ppm"
exit 0
