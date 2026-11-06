# Family Pack visual navigation smoke helper.
# Usage: .\tools\fp-visual-nav\run_nav.ps1 -Frames 360 -Seq 'A:180:10' -Tag probe360
param(
    [int]$Frames = 360,
    [string]$Seq = "",
    [string]$Tag = "run",
    [int]$DumpEvery = 15,
    [string]$Mod = "D:\Temp\zeebo_all\Zeebo_Family_Pack__Brazil___Es_Pt_\mod\277229\game.mod",
    [string]$Build = "build-fp-visual-nav\Release"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $root

$art = Join-Path $root "artifacts\fp-visual-nav\$Tag"
$dump = Join-Path $art "frames"
$keep = Join-Path $art "key_frames"
New-Item -ItemType Directory -Force -Path $dump, $keep | Out-Null

$smoke = Join-Path $root "$Build\libretro_smoke.exe"
$core = Join-Path $root "$Build\lozeebo_libretro.dll"
$finalPpm = Join-Path $art "final.ppm"
$log = Join-Path $art "run.log"

if (-not (Test-Path $smoke)) { throw "missing $smoke" }
if (-not (Test-Path $core)) { throw "missing $core" }
if (-not (Test-Path $Mod)) { throw "missing $Mod" }

Remove-Item "$dump\frame_*.ppm" -ErrorAction SilentlyContinue
$env:ZEEMU_SMOKE_DUMP_DIR = $dump
$env:ZEEMU_SMOKE_DUMP_EVERY = "$DumpEvery"
if ($Seq) {
    $env:ZEEMU_SMOKE_SEQ = $Seq
    # disable default single-button press so only SEQ drives input
    $env:ZEEMU_SMOKE_HOLD_FRAMES = "0"
    Remove-Item Env:ZEEMU_SMOKE_BUTTON -ErrorAction SilentlyContinue
    Remove-Item Env:ZEEMU_SMOKE_PRESS_FRAME -ErrorAction SilentlyContinue
} else {
    Remove-Item Env:ZEEMU_SMOKE_SEQ -ErrorAction SilentlyContinue
}

Write-Host "RUN tag=$Tag frames=$Frames seq='$Seq'"
$sw = [Diagnostics.Stopwatch]::StartNew()
& $smoke $core $Mod $finalPpm $Frames 2> $log
$code = $LASTEXITCODE
$sw.Stop()
Write-Host "exit=$code elapsed=$($sw.Elapsed.TotalSeconds)s"

$summary = Join-Path $art "classify.json"
python (Join-Path $PSScriptRoot "classify_ppm.py") $dump --keep $keep --json $summary
$cls = $LASTEXITCODE

# compact: keep only key frames + final + log + json; drop solid dump mass after classify
$keepCount = (Get-ChildItem $keep -Filter "frame_*.ppm" -ErrorAction SilentlyContinue | Measure-Object).Count
$frameCount = (Get-ChildItem $dump -Filter "frame_*.ppm" -ErrorAction SilentlyContinue | Measure-Object).Count
Write-Host "frames=$frameCount key=$keepCount classify_exit=$cls"
# prune dump dir to free space (keys already copied)
Remove-Item "$dump\frame_*.ppm" -ErrorAction SilentlyContinue
Remove-Item $dump -Force -ErrorAction SilentlyContinue

Select-String -Path $log -Pattern "SMOKE-SUMMARY|SMOKE-SEQ|IMedia_Play: MP3|game_selected|familypack\.png|DrawArrays\[0\]" |
    ForEach-Object { $_.Line }

exit $code
