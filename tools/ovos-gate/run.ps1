# Ovos gate - criterios @ mixer pos_fp fix + heap auto-Play (2026-11-07).
# Pass: exit 0, video_varied=1, video_ever_varied=1, audio_nonzero>0,
#       0 invalid/STACK/descarril. Sem fake HandleEvent(evt=2) CLSID 0x010963A5.
# -Playable: SEQ longa + PPM com amarelo ovo (ffcc33) / black<0.5.
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

if ($Playable) {
    $Frames = 900
    $env:ZEEMU_SMOKE_MAX_SECONDS = "300"
    $env:ZEEMU_SMOKE_SEQ = "A:60:15,A:120:15,START:180:15,A:240:15,START:300:15,A:360:20,UP:420:10,DOWN:450:10,A:480:20,START:540:20,A:600:20,RIGHT:660:15,LEFT:700:15,A:750:20,START:810:20"
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
    # PPM: ovo amarelo ffcc33 / black fraction < 0.5
    $py = @"
from collections import Counter
raw=open(r'$($ppm.Replace('\','\\'))','rb').read()
i=raw.index(b'\n',2)+1; i2=raw.index(b'\n',i)+1; i3=raw.index(b'\n',i2)+1
pix=raw[i3:]; n=len(pix)//3
c=Counter(pix[j:j+3] for j in range(0,len(pix),3))
black=c[b'\x00\x00\x00']/n
yellow=sum(v for k,v in c.items() if k[0]>=0xe0 and k[1]>=0x90 and k[1]<=0xe0 and k[2]<=0x60)
print(f'black={black:.4f} yellow={yellow} unique={len(c)}')
raise SystemExit(0 if (black < 0.5 and yellow > 1000) else 1)
"@
    $pyFile = Join-Path $out "ppm_check.py"
    Set-Content -Path $pyFile -Value $py -Encoding UTF8
    python $pyFile
    if ($LASTEXITCODE -ne 0) { $fail += "PPM not egg/menu (black>=0.5 or yellow<=1000)" }
    if ($raw -notmatch "hid-button") { $fail += "no hid-button" }
}

if ($fail.Count -gt 0) {
    Fail ($fail -join ", ")
}

$hash = (Get-FileHash $ppm -Algorithm SHA256).Hash
Write-Host "PASS exit=0 varied=1 ever=1 audio_nonzero=$audio invalids=0 ppm=$hash"
exit 0
