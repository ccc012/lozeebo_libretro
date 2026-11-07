# Run from the repository root after a CMake Release build.
param([string]$Build = 'build-input\Release',
      [string]$Mod = 'D:\Temp\zeebo_all\Zeebo_Family_Pack__Brazil___Es_Pt_\mod\277229\game.mod')
$ErrorActionPreference = 'Stop'
$out = Join-Path (Get-Location) 'artifacts\fp-selection'
New-Item -ItemType Directory -Force $out | Out-Null
$names = @('ZEEMU_SMOKE_SEQ', 'ZEEMU_SMOKE_HOLD_FRAMES', 'ZEEMU_SMOKE_MAX_SECONDS', 'ZEEMU_TRACE_HID', 'ZEEMU_SMOKE_DUMP_DIR')
$saved = @{}
foreach ($name in $names) { $saved[$name] = [Environment]::GetEnvironmentVariable($name) }
try {
    $env:ZEEMU_SMOKE_HOLD_FRAMES = '0'
    $env:ZEEMU_SMOKE_MAX_SECONDS = '120'
    $env:ZEEMU_TRACE_HID = '1'
    Remove-Item Env:ZEEMU_SMOKE_DUMP_DIR -ErrorAction SilentlyContinue
    $passed = 0
    foreach ($hold in @(1, 2, 5, 10, 30)) {
        $env:ZEEMU_SMOKE_SEQ = "DOWN:950:$hold,B:1100:10"
        $log = Join-Path $out "hold-$hold.log"
        $ppm = Join-Path $out "hold-$hold.ppm"
        # cmd avoids Windows PowerShell treating native stderr logs as terminating errors.
        $command = '"{0}\libretro_smoke.exe" "{0}\lozeebo_libretro.dll" "{1}" "{2}" 1350 2>"{3}"' -f $Build, $Mod, $ppm, $log
        & cmd /c $command
        if ($LASTEXITCODE -ne 0) { throw "hold=$hold smoke exit=$LASTEXITCODE" }
        $text = Get-Content $log -Raw
        if ($text -notmatch 'IFileMgr_OpenFile: ''cannonball\\gfx\\bird.png''' -or
            $text -notmatch '\[SMOKE-SUMMARY\].*video_frames=1350' -or
            $text -cmatch '\binvalid\b|\bSTACK\b|descarril') {
            throw "hold=$hold did not load Cannonball cleanly; see $log"
        }
        $passed++
        Write-Host "PASS hold=$hold target=Cannonball"
    }
    Write-Host "PASS selection=$passed/5"
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $saved[$name]) }
}
