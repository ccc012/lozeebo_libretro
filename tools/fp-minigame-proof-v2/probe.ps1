param(
    [int]$Frames = 360,
    [string]$Seq = "",
    [string]$Tag = "probe",
    [int]$MaxSeconds = 150,
    [string]$Mod = "D:\Temp\zeebo_all\Zeebo_Family_Pack__Brazil___Es_Pt_\mod\277229\game.mod",
    [string]$Build = "build-fp-proof\Release"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $root
$out = Join-Path $root "artifacts\fp-minigame-proof-v2\$Tag"
New-Item -ItemType Directory -Force $out | Out-Null
$env:ZEEMU_SMOKE_MAX_SECONDS = "$MaxSeconds"
$env:ZEEMU_SMOKE_HOLD_FRAMES = "0"
if ($Seq) { $env:ZEEMU_SMOKE_SEQ = $Seq } else { Remove-Item Env:ZEEMU_SMOKE_SEQ -ErrorAction SilentlyContinue }
& (Join-Path $root "$Build\libretro_smoke.exe") `
   (Join-Path $root "$Build\lozeebo_libretro.dll") $Mod `
   (Join-Path $out "final.ppm") $Frames 2> (Join-Path $out "run.log")
$code = $LASTEXITCODE
Select-String (Join-Path $out "run.log") -Pattern "SMOKE-TIMING|SMOKE-SUMMARY|SMOKE-SEQ|hid-button|hid-position" | ForEach-Object Line
exit $code
