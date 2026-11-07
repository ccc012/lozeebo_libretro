param(
    [int]$Frames = 600,
    [string]$Mod = "D:\Temp\zeebo_all\Double_Dragon__Brazil___Es_Pt_\mod\274754\ddragonz.mod",
    [string]$Build = "build-dd\Release"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $root

if (-not (Test-Path $Mod)) {
    $fallback = Join-Path $root "tests\roms\real_ddragon_game.mod"
    if (Test-Path $fallback) { $Mod = $fallback }
    else { throw "MOD ausente: $Mod" }
}

$out = Join-Path $root "artifacts\dd-gate"
New-Item -ItemType Directory -Force $out | Out-Null
$ppm = Join-Path $out ("dd-{0}.ppm" -f $Frames)
$log = Join-Path $out ("dd-{0}.log" -f $Frames)
$smoke = Join-Path $root "$Build\libretro_smoke.exe"
$dll = Join-Path $root "$Build\lozeebo_libretro.dll"
if (-not (Test-Path $smoke)) { throw "smoke ausente: $smoke" }
if (-not (Test-Path $dll)) { throw "dll ausente: $dll" }

$env:ZEEMU_SMOKE_MAX_SECONDS = [string]([Math]::Max(120, [int]($Frames / 2)))
Remove-Item Env:ZEEMU_SMOKE_SEQ -ErrorAction SilentlyContinue
Remove-Item Env:ZEEMU_SMOKE_DUMP_DIR -ErrorAction SilentlyContinue
Remove-Item Env:ZEEMU_SMOKE_HOLD_FRAMES -ErrorAction SilentlyContinue

$p = Start-Process -FilePath $smoke `
    -ArgumentList @($dll, $Mod, $ppm, "$Frames") `
    -RedirectStandardError $log `
    -NoNewWindow -PassThru -Wait
$code = $p.ExitCode
if ($code -ne 0) { throw "exit $code (esperado 0)" }

$text = Get-Content -Raw $log
if ($text -notmatch "\[SMOKE-SUMMARY\].*?video_varied=1") {
    throw "video_varied!=1 ou SUMMARY ausente"
}
if ($text -notmatch "audio_nonzero=([1-9]\d*)") {
    throw "audio_nonzero=0 ou ausente"
}
$sumLines = @(Select-String -Path $log -Pattern "SMOKE-SUMMARY|audio_frames=")
$sum = ($sumLines | ForEach-Object { $_.Line.Trim() }) -join " "

$gfx = ([regex]::Matches($text, "real_create_instance: GRAPHICS")).Count
if ($gfx -gt 2) { throw "GRAPHICS CreateInstance=$gfx (retry loop; fake evt=2?)" }
$hidRel = ([regex]::Matches($text, "IHID Release:")).Count
if ($hidRel -gt 0) { throw "IHID Release=$hidRel (fake evt=2 cancelou HID)" }

$data = ([regex]::Matches($text, "OpenFile: 'data\.ggz'")).Count
if ($data -lt 1) { throw "data.ggz nao aberto (load nao avancou)" }

$hard = ([regex]::Matches($text, "descarril|STACK overflow|FATAL|INVALID opcode")).Count
if ($hard -gt 0) { throw "falhas hard=$hard no log" }

$hash = (Get-FileHash $ppm -Algorithm SHA256).Hash
Write-Host "PASS frames=$Frames exit=0 varied=1 audio_nz ok data.ggz=$data GRAPHICS=$gfx"
Write-Host "SUMMARY $sum"
Write-Host "PPM_SHA256 $hash"
Write-Host "LOG $log"
Write-Host "PPM $ppm"
exit 0
