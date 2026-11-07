# Double Dragon regression gate — FAIL loud (exit 1).
# Usage: powershell -File tools/dd-gate/run.ps1 [-Frames 600] [-Seq "..."] [-Playable]
#   -Playable: SEQ longa (START→stage→LEFT/RIGHT hold + A/B punches) com dump
#   temporal e prova de PPM distintos por acao (movimento vs soco).
param(
    [int]$Frames = 600,
    [string]$Mod = "D:\Temp\zeebo_all\Double_Dragon__Brazil___Es_Pt_\mod\274754\ddragonz.mod",
    [string]$BuildDir = "build-dd-gate",
    [string]$Seq = "",
    [switch]$SkipBuild,
    [switch]$Playable
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

# SEQ default para -Playable: START duplo (avanca title→stage),
# depois LEFT hold 90f, RIGHT hold 90f, A/B punches intercalados.
$playableSeq = "START:340:8,START:420:8,LEFT:560:90,RIGHT:700:90,A:820:6,A:860:6,A:900:6,B:960:6,B:1000:6,LEFT:1060:60,A:1140:6,RIGHT:1180:60,A:1260:6"

if ($Playable) {
    $Frames = 1400
    $Seq = $playableSeq
}

$tag = if ($Playable) { "playable" } elseif ($Seq) { "seq-{0}" -f $Frames } else { "{0}" -f $Frames }
$ppm = Join-Path $out ("dd-{0}.ppm" -f $tag)
$log = Join-Path $out ("dd-{0}.log" -f $tag)
Remove-Item $ppm, $log -ErrorAction SilentlyContinue

$env:ZEEMU_SMOKE_MAX_SECONDS = [string]([Math]::Max(180, [int]($Frames / 2)))
Remove-Item Env:ZEEMU_SMOKE_DUMP_DIR -ErrorAction SilentlyContinue
if ($Seq) {
    $env:ZEEMU_SMOKE_SEQ = $Seq
    $env:ZEEMU_SMOKE_HOLD_FRAMES = "0"
} else {
    Remove-Item Env:ZEEMU_SMOKE_SEQ -ErrorAction SilentlyContinue
    Remove-Item Env:ZEEMU_SMOKE_HOLD_FRAMES -ErrorAction SilentlyContinue
}

$dumpDir = $null
if ($Playable) {
    $dumpDir = Join-Path $out "dump-$tag"
    New-Item -ItemType Directory -Force $dumpDir | Out-Null
    Remove-Item (Join-Path $dumpDir "*.ppm") -ErrorAction SilentlyContinue
    $env:ZEEMU_SMOKE_DUMP_DIR = $dumpDir
    $env:ZEEMU_SMOKE_DUMP_STRIDE = "30"
}

Write-Host "smoke ${Frames}f mod=$Mod seq=$Seq playable=$Playable"
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

# Title idle SHA conhecido (nao pode aparecer apos SEQ/Playable)
$idleTitle = "2149E446D5DC3A50D2348E052C516C5CE0F133CEA26C7072A92117672E7E518A"

if ($Seq -or $Playable) {
    if ($text -notmatch "hid-button") {
        Fail "SEQ setada mas hid-button ausente no log"
    }
    $hash = (Get-FileHash $ppm -Algorithm SHA256).Hash
    if ($hash -eq $idleTitle) {
        Fail "PPM ainda e title idle apos SEQ (START nao avancou)"
    }
} else {
    $hash = (Get-FileHash $ppm -Algorithm SHA256).Hash
}

if ($Playable) {
    # Dump temporal: precisa de pelo menos 3 PPM distintos na fase stage
    # (pos-START) provando que input causa mudanca visual.
    $dumpPpms = Get-ChildItem (Join-Path $dumpDir "*.ppm") -ErrorAction SilentlyContinue
    if (-not $dumpPpms -or $dumpPpms.Count -lt 10) {
        Fail "dump temporal insuficiente ($($dumpPpms.Count) frames)"
    }
    $hashes = @()
    foreach ($f in $dumpPpms) {
        $hashes += (Get-FileHash $f.FullName -Algorithm SHA256).Hash
    }
    $unique = ($hashes | Sort-Object -Unique).Count
    if ($unique -lt 10) {
        Fail "dump muito homogeneo: $unique PPM unicos (need >=10)"
    }

    # Prova de movimento: PPM durante LEFT hold != PPM durante RIGHT hold
    # Frames aprox: LEFT ~560-649 (dump@570,600,630,660), RIGHT ~700-789 (dump@720,750,780)
    $leftFrames = @('frame_0570','frame_0600','frame_0630','frame_0660')
    $rightFrames = @('frame_0720','frame_0750','frame_0780')
    $leftHashes = @(); $rightHashes = @()
    foreach ($n in $leftFrames) {
        $p = Join-Path $dumpDir "$n.ppm"
        if (Test-Path $p) { $leftHashes += (Get-FileHash $p -Algorithm SHA256).Hash }
    }
    foreach ($n in $rightFrames) {
        $p = Join-Path $dumpDir "$n.ppm"
        if (Test-Path $p) { $rightHashes += (Get-FileHash $p -Algorithm SHA256).Hash }
    }
    if ($leftHashes.Count -lt 2 -or $rightHashes.Count -lt 2) {
        Fail "frames de movimento LEFT/RIGHT ausentes no dump"
    }
    # LEFT e Right devem ter conjuntos disjuntos de hashes (movimento diferente)
    $common = Compare-Object $leftHashes $rightHashes -IncludeEqual -ExcludeDifferent
    if ($common.Count -gt 0) {
        Fail "LEFT e RIGHT produziram PPM identicos (sem prova de movimento)"
    }

    # Prova de soco: A punch frame != pre-A frame
    $preA = Join-Path $dumpDir "frame_0810.ppm"
    $punchA = Join-Path $dumpDir "frame_0840.ppm"
    if ((Test-Path $preA) -and (Test-Path $punchA)) {
        $hPre = (Get-FileHash $preA -Algorithm SHA256).Hash
        $hPunch = (Get-FileHash $punchA -Algorithm SHA256).Hash
        if ($hPre -eq $hPunch) {
            Fail "A punch nao mudou PPM vs pre-A (sem prova de acao)"
        }
    }

    Write-Host "PLAYABLE dump=$($dumpPpms.Count) unique=$unique LEFT_distinct=$($leftHashes.Count) RIGHT_distinct=$($rightHashes.Count)"
}

Write-Host "PASS frames=$Frames exit=0 varied=1 audio_nz ok data.ggz=$data GRAPHICS=$gfx DrawArrays=$draw"
Write-Host "SUMMARY $sum"
Write-Host "PPM_SHA256 $hash"
Write-Host "LOG $log"
Write-Host "PPM $ppm"
exit 0
