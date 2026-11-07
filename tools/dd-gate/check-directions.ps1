# Transport regression: DD must receive directional press AND release UIDs.
# This does not prove character displacement; gameplay comparison remains separate.
param(
    [string]$Mod = 'D:\Temp\zeebo_all\Double_Dragon__Brazil___Es_Pt_\mod\274754\ddragonz.mod',
    [string]$BuildDir = 'build-fp-gate'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$out = Join-Path $root 'artifacts\dd-directions'
New-Item -ItemType Directory -Force $out | Out-Null
$smoke = Join-Path $root "$BuildDir\Release\libretro_smoke.exe"
$dll = Join-Path $root "$BuildDir\Release\lozeebo_libretro.dll"
$ppm = Join-Path $out 'final.ppm'
$log = Join-Path $out 'input.log'
$names = @('ZEEMU_TRACE_HID','ZEEMU_SMOKE_SEQ','ZEEMU_SMOKE_HOLD_FRAMES','ZEEMU_SMOKE_MAX_SECONDS')
$previous = @{}
foreach ($name in $names) { $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    $env:ZEEMU_TRACE_HID = '1'
    $env:ZEEMU_SMOKE_HOLD_FRAMES = '0'
    $env:ZEEMU_SMOKE_MAX_SECONDS = '120'
    $env:ZEEMU_SMOKE_SEQ = 'START:340:8,START:420:8,LEFT:560:30,RIGHT:610:30,UP:660:30,DOWN:710:30'
    cmd /c "`"$smoke`" `"$dll`" `"$Mod`" `"$ppm`" 760 2>`"$log`""
    if ($LASTEXITCODE -ne 0) { throw "Smoke failed: $LASTEXITCODE; $log" }
    $text = Get-Content -Raw $log
    foreach ($uid in @('0106C3FE','0106C3FF','0106C400','0106C401')) {
        foreach ($down in @(0,1)) {
            if ($text -notmatch "HID: uid=0x$uid down=$down\b") { throw "Missing direction $uid down=$down; $log" }
        }
    }
    Write-Host 'PASS DD directional press/release transport (not gameplay proof)'
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process') }
}
