param(
    [int]$Frames = 900,
    [string]$Mod = "D:\Temp\zeebo_all\Zeebo_Family_Pack__Brazil___Es_Pt_\mod\277229\game.mod",
    [string]$Build = "build-fp-final\Release"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $root
$out = Join-Path $root "artifacts\fp-final"
New-Item -ItemType Directory -Force $out | Out-Null
$env:ZEEMU_SMOKE_MAX_SECONDS = "240"
$env:ZEEMU_SMOKE_HOLD_FRAMES = "0"
$env:ZEEMU_SMOKE_SEQ = "A:240:10,A:300:10,RIGHT:420:120,LEFT:570:120,A:720:60"
& (Join-Path $root "$Build\libretro_smoke.exe") `
   (Join-Path $root "$Build\lozeebo_libretro.dll") $Mod `
   (Join-Path $out "minigame.ppm") $Frames 2> (Join-Path $out "minigame.log")
exit $LASTEXITCODE
