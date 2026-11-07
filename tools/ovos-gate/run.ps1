# Ovos gate - criterios @ mixer pos_fp + heap auto-Play + media_tick + HID queue
# (2026-11-07).
# Pass: exit 0, video_varied=1, video_ever_varied=1, audio_nonzero>0,
#       0 invalid/STACK/descarril. Sem fake HandleEvent(evt=2) CLSID 0x010963A5.
# -Playable: SEQ shake pos-ovo → rachadura → starfield; audio continuo (MP3);
#            dump com fase amarela E fase starfield; hid-button.
param(
    [int]$Frames = 180,
    [string]$Mod = "D:\Temp\zeebo_all\Um_Jogo_de_Ovos__Brazil___Es_Pt_\mod\279036\game.mod",
    [string]$BuildDir = "build-ovos",
    [string]$OutDir = "artifacts\ovos-gate",
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

if (-not (Test-Path $Mod)) { Fail "missing mod $Mod" }

$rel = Join-Path $root "$BuildDir\Release"
$smoke = Join-Path $rel "libretro_smoke.exe"
$core  = Join-Path $rel "lozeebo_libretro.dll"

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

if (-not (Test-Path $smoke)) { Fail "missing $smoke" }
if (-not (Test-Path $core))  { Fail "missing $core" }

$out = Join-Path $root $OutDir
New-Item -ItemType Directory -Force $out | Out-Null
$tag = if ($Playable) { "playable" } else { "f$Frames" }
$ppm = Join-Path $out ("{0}.ppm" -f $tag)
$log = Join-Path $out ("{0}.log" -f $tag)
Remove-Item $ppm, $log -ErrorAction SilentlyContinue

Remove-Item Env:ZEEMU_SMOKE_BUTTON -ErrorAction SilentlyContinue
Remove-Item Env:ZEEMU_SMOKE_PRESS_FRAME -ErrorAction SilentlyContinue
Remove-Item Env:ZEEMU_SMOKE_DUMP_DIR -ErrorAction SilentlyContinue
$env:ZEEMU_SMOKE_HOLD_FRAMES = "0"
$env:ZEEMU_SMOKE_MAX_SECONDS = [string]([Math]::Max(120, [int]($Frames / 2) + 60))

$dumpDir = $null
if ($Playable) {
    $Frames = 1600
    $env:ZEEMU_SMOKE_MAX_SECONDS = "400"
    # Shake D-pad+B apos ovo (~f1000) → rachadura → starfield + MP3
    $seq = @()
    for ($f = 1000; $f -lt 1320; $f += 8) {
        $dirs = @('UP','RIGHT','DOWN','LEFT','B')
        $seq += ("{0}:{1}:7" -f $dirs[(($f / 8) % 5)], $f)
    }
    for ($f = 1350; $f -lt 1550; $f += 10) {
        $dirs = @('UP','DOWN','LEFT','RIGHT','B','A')
        $seq += ("{0}:{1}:8" -f $dirs[(($f / 10) % 6)], $f)
    }
    $env:ZEEMU_SMOKE_SEQ = ($seq -join ",")
    $dumpDir = Join-Path $out "playable-dump"
    New-Item -ItemType Directory -Force $dumpDir | Out-Null
    Remove-Item (Join-Path $dumpDir "*.ppm") -ErrorAction SilentlyContinue
    $env:ZEEMU_SMOKE_DUMP_DIR = $dumpDir
    $env:ZEEMU_SMOKE_DUMP_STRIDE = "20"
    $ppm = Join-Path $out "playable.ppm"
    $log = Join-Path $out "playable.log"
    Remove-Item $ppm, $log -ErrorAction SilentlyContinue
} else {
    Remove-Item Env:ZEEMU_SMOKE_SEQ -ErrorAction SilentlyContinue
}

Write-Host "smoke ${Frames}f mod=$Mod playable=$Playable"
$cmdLine = "`"$smoke`" `"$core`" `"$Mod`" `"$ppm`" $Frames 2>`"$log`""
cmd /c $cmdLine
$exit = $LASTEXITCODE

$summary = Select-String -Path $log -Pattern "\[SMOKE-SUMMARY\]" -ErrorAction SilentlyContinue | Select-Object -Last 1
if (-not $summary) {
    Fail "no SMOKE-SUMMARY (exit=$exit)"
}
$line = $summary.Line
Write-Host $line

$varied = if ($line -match "video_varied=(\d+)") { [int]$Matches[1] } else { -1 }
$ever   = if ($line -match "video_ever_varied=(\d+)") { [int]$Matches[1] } else { -1 }
$audio  = if ($line -match "audio_nonzero=(\d+)") { [int64]$Matches[1] } else { -1 }
$raw = Get-Content $log -Raw
$badInvalid = ([regex]::Matches($raw, "(?i)\binvalid\b")).Count
$badStack   = ([regex]::Matches($raw, "\bSTACK\b")).Count
$badDesc    = ([regex]::Matches($raw, "(?i)descarril")).Count
$fakeEvt2   = ([regex]::Matches($raw, "HandleEvent\(evt=2\)")).Count

$fail = @()
if ($exit -ne 0) { $fail += "exit=$exit" }
if ($varied -ne 1) { $fail += "video_varied=$varied" }
if ($ever -ne 1) { $fail += "video_ever_varied=$ever" }
if ($audio -lt 1) { $fail += "audio_nonzero=$audio (need >0)" }
if ($badInvalid -gt 0) { $fail += "invalid=$badInvalid" }
if ($badStack -gt 0) { $fail += "STACK=$badStack" }
if ($badDesc -gt 0) { $fail += "descarril=$badDesc" }
if ($fakeEvt2 -gt 0) { $fail += "fake HandleEvent(evt=2)=$fakeEvt2" }

if ($Playable) {
    # Dump: fase ovo amarelo E fase starfield pos-quebra (black~0.996, white~650)
    $dumpEsc = $dumpDir.Replace('\','\\')
    $py = @"
from collections import Counter
import glob, os
egg=False; star=False; crack=False
for p in sorted(glob.glob(r'$dumpEsc\\frame_*.ppm')):
    raw=open(p,'rb').read()
    i=raw.index(b'\n',2)+1; i2=raw.index(b'\n',i)+1; i3=raw.index(b'\n',i2)+1
    pix=raw[i3:]; n=len(pix)//3
    c=Counter(pix[j:j+3] for j in range(0,len(pix),3))
    black=c[b'\x00\x00\x00']/n
    yellow=sum(v for k,v in c.items() if k[0]>=0xe0 and 0x90<=k[1]<=0xe0 and k[2]<=0x60)
    white=c[b'\xff\xff\xff']
    if black < 0.5 and yellow > 10000: egg=True
    if 0.2 < black < 0.5 and yellow > 50000: crack=True
    if black > 0.99 and 400 < white < 2000 and len(c) < 40: star=True
nd=len(glob.glob(r'$dumpEsc\\frame_*.ppm'))
print(f'egg={int(egg)} crack={int(crack)} star={int(star)} dumps={nd}')
raise SystemExit(0 if (egg and star) else 1)
"@
    $pyFile = Join-Path $out "ppm_check.py"
    Set-Content -Path $pyFile -Value $py -Encoding UTF8
    python $pyFile
    if ($LASTEXITCODE -ne 0) { $fail += "missing egg→starfield phases in dump" }
    if ($raw -notmatch "hid-button") { $fail += "no hid-button" }
    if ($raw -notmatch "IMedia_Play: MP3") { $fail += "no MP3 Play (post-crack audio)" }
    if ($audio -lt 100000) { $fail += "audio_nonzero=$audio (need >=100000 continuous)" }
}

if ($fail.Count -gt 0) {
    Fail ($fail -join ", ")
}

if (-not (Test-Path $ppm) -and $dumpDir) {
    $last = Get-ChildItem (Join-Path $dumpDir "frame_*.ppm") -ErrorAction SilentlyContinue |
        Sort-Object Name | Select-Object -Last 1
    if ($last) { Copy-Item $last.FullName $ppm -Force }
}
$hash = if (Test-Path $ppm) { (Get-FileHash $ppm -Algorithm SHA256).Hash } else { "none" }
Write-Host "PASS exit=0 varied=1 ever=1 audio_nonzero=$audio invalids=0 ppm=$hash"
exit 0
