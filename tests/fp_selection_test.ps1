# FP selection/minigame verification test
# Sets up environment, runs smoke test, checks frame variation between menu and after selection

$archive = 'D:\Games\Zeebo - Zeebo\Zeebo Family Pack (Brazil) (Es,Pt).7z'
$member = 'mod/277229/game.mod'
$seq = 'A:240:10,A:300:10,RIGHT:420:120,LEFT:570:120,A:720:60'
$press = '120'
$hold = '10'
$dumpDir = "$env:TEMP\fp_test_frames"
if (!(Test-Path $dumpDir)) { New-Item -ItemType Directory -Path $dumpDir | Out-Null }

$env:ZEEMU_SMOKE_ARCHIVE = $archive
$env:ZEEMU_SMOKE_MEMBER = $member
$env:ZEEMU_SMOKE_SEQ = $seq
$env:ZEEMU_SMOKE_PRESS_FRAME = $press
$env:ZEEMU_SMOKE_HOLD_FRAMES = $hold
$env:ZEEMU_SMOKE_DUMP_DIR = $dumpDir

$exe = "C:\Users\Lucas\AppData\Local\Temp\opencode\progress-fp-gameplay\build-progress\Release\libretro_smoke.exe"
$core = "C:\Users\Lucas\AppData\Local\Temp\opencode\progress-fp-gameplay\build-progress\Release\lozeebo_libretro.dll"
$rom = "C:\Users\Lucas\AppData\Local\Temp\opencode\progress-fp-gameplay\artifacts\fp-gameplay\content\game.mod"

& $exe $core $rom 900
if ($LASTEXITCODE -ne 0) { Write-Error "Smoke run failed"; exit 1 }

# compare frame 120 (menu) and 300 (post‑selection)
$frameMenu = Join-Path $dumpDir "frame_0120.ppm"
$frameSel = Join-Path $dumpDir "frame_0300.ppm"
if (!(Test-Path $frameMenu) -or !(Test-Path $frameSel)) { Write-Error "Frames missing"; exit 1 }
$hashMenu = (Get-FileHash -Algorithm SHA256 $frameMenu).Hash
$hashSel = (Get-FileHash -Algorithm SHA256 $frameSel).Hash
if ($hashMenu -eq $hashSel) { Write-Error "No visual change between menu and selection"; exit 1 }

Write-Host "PASS: visual change detected"
exit 0
