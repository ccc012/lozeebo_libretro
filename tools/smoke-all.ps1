param([int]$Frames=180,[string]$BuildDir='build-fp-gate',[int]$Throttle=6)
$ErrorActionPreference='Continue'
$root=Split-Path -Parent $PSScriptRoot
$smoke=Join-Path $root "$BuildDir\Release\libretro_smoke.exe"
$dll  =Join-Path $root "$BuildDir\Release\lozeebo_libretro.dll"
$art  =Join-Path $root 'artifacts\smoke-all'
New-Item -ItemType Directory -Force $art | Out-Null

$games = @(Get-ChildItem 'D:\Games\Zeebo - Zeebo\*.7z' | Sort-Object Name)

$jobs = @()
foreach ($g in $games) {
    while (($jobs | Where-Object {$_.State -eq 'Running'}).Count -ge $Throttle) { Start-Sleep -Milliseconds 500 }
    $zip=$g.FullName
    $label=$g.BaseName -replace ' \(Brazil\) \(Es,Pt\)',''
    $safeName=$label -replace '[<>:"/\\|?*]','_'
    $log=Join-Path $art "$safeName.log"
    $ppm=Join-Path $art "$safeName.ppm"
    $jobs += Start-Job -Name $label -ScriptBlock {
        param($smoke,$dll,$zip,$ppm,$log,$Frames,$label)
        $env:ZEEMU_SMOKE_MAX_SECONDS='40'
        $env:ZEEMU_SMOKE_HOLD_FRAMES='0'
        cmd /c "`"$smoke`" `"$dll`" `"$zip`" `"$ppm`" $Frames 2>`"$log`""
        $exit=$LASTEXITCODE
        $text=Get-Content -Raw $log -ErrorAction SilentlyContinue
        $vv  =if($text -match 'video_varied=1'){'varied'}elseif($text -match 'video_varied=0'){'uniform'}else{'?'}
        $audio='?'; if($text -match 'audio_nonzero=(\d+)'){$audio=$Matches[1]}
        $err  =if($text -match 'retro_load_game failed|zmod_load: sem dados'){'LOAD_FAIL'}
               elseif($text -match 'STACK OVERFLOW|descarril|EXCEPTION'){'GUEST_FAULT'}
               else{''}
        [pscustomobject]@{Label=$label;Exit=$exit;Video=$vv;Audio=$audio;Error=$err}
    } -ArgumentList $smoke,$dll,$zip,$ppm,$log,$Frames,$label
}

$res = $jobs | Wait-Job | Receive-Job
$jobs | Remove-Job -Force

$out = $res | Sort-Object Label
$out | Format-Table -AutoSize Label,Exit,Video,Audio,Error | Tee-Object (Join-Path $art 'summary.txt')

Write-Host "`nVIDEO-VARIED:" -ForegroundColor Green
$out | Where-Object {$_.Video -eq 'varied'} | Format-Table -AutoSize Label,Audio,Error

Write-Host "BLACK/FAIL:" -ForegroundColor Red
$out | Where-Object {$_.Video -ne 'varied'} | Format-Table -AutoSize Label,Exit,Error
