# Varredura dos jogos do romset externo contra o smoke test real.
# Classifica cada jogo pela ultima linha relevante do log de stderr.
# Uso: .\sweep_68_games.ps1 [-Dll caminho\zeebo_libretro.dll] [-Smoke caminho\libretro_smoke.exe] [-Out saida.csv]

param(
    [string]$Dll = "$PSScriptRoot\x64\Release\zeebo_libretro.dll",
    [string]$Smoke = "C:\Users\Lucas\source\repos\zeebo_libretro\tests\libretro_smoke.exe",
    [string]$RomBase = "C:\Users\Lucas\Downloads\zeebo-romset-and-devtools\Zeebo\Zeebo\Zeebo Game & App Compilation - OpenZeebo\274804",
    [string]$Out = "$PSScriptRoot\sweep_result.csv",
    [int]$TimeoutSec = 40
)

if (-not (Test-Path $Dll)) { Write-Host "DLL nao encontrada: $Dll" -ForegroundColor Red; exit 1 }
if (-not (Test-Path $Smoke)) { Write-Host "smoke test nao encontrado: $Smoke" -ForegroundColor Red; exit 1 }

$modRoot = Join-Path $RomBase "mod"
$excludeDirs = @("id1", "nfsresources", "preyresources", "quake2res")

$dirs = Get-ChildItem $modRoot -Directory | Where-Object { $excludeDirs -notcontains $_.Name } | Sort-Object Name

$results = @()
$i = 0
foreach ($d in $dirs) {
    $i++
    $modFile = Get-ChildItem $d.FullName -Filter "*.mod" -File | Select-Object -First 1
    if (-not $modFile) {
        $results += [pscustomobject]@{ id = $d.Name; arquivo = ""; classificacao = "sem .mod"; detalhe = "" ; fb0 = ""; instrucoes = "" }
        Write-Host "[$i/$($dirs.Count)] $($d.Name): sem .mod" -ForegroundColor DarkGray
        continue
    }

    $proc = Start-Process -FilePath $Smoke -ArgumentList "`"$Dll`"", "`"$($modFile.FullName)`"" `
        -NoNewWindow -PassThru -RedirectStandardError "$env:TEMP\sweep_err.txt" -RedirectStandardOutput "$env:TEMP\sweep_out.txt"

    $finished = $proc.WaitForExit($TimeoutSec * 1000)
    if (-not $finished) {
        try { $proc.Kill() } catch {}
        $results += [pscustomobject]@{ id = $d.Name; arquivo = $modFile.Name; classificacao = "timeout (>$TimeoutSec s)"; detalhe = "" ; fb0 = ""; instrucoes = "" }
        Write-Host "[$i/$($dirs.Count)] $($d.Name) ($($modFile.Name)): TIMEOUT" -ForegroundColor Red
        continue
    }

    $log = Get-Content "$env:TEMP\sweep_err.txt" -Raw -ErrorAction SilentlyContinue
    if (-not $log) { $log = "" }

    # Classificacao pela ultima linha relevante
    $classificacao = "desconhecido"
    $detalhe = ""
    $fb0 = ""
    $instrucoes = ""

    if ($log -match "CPU descarrilou: fetch em (0x[0-9A-Fa-f]+)") {
        $classificacao = "descarrilou"
        $detalhe = $Matches[1]
    }
    elseif ($log -match "zmod_load: falha ao copiar ROM" -or $log -match "e um ZIP" -or $log -match "e um GZIP") {
        $classificacao = "formato nao aceito"
        $detalhe = ($log -split "`n" | Select-String "zmod_load:" | Select-Object -Last 1).ToString().Trim()
    }
    elseif ($log -match "CLSID desconhecido") {
        $classificacao = "CLSID desconhecido"
        $detalhe = ($log -split "`n" | Select-String "CLSID desconhecido" | Select-Object -Last 1).ToString().Trim()
    }
    elseif ($log -match "boot: AEEMod_Load falhou") {
        $classificacao = "AEEMod_Load falhou"
    }
    elseif ($log -match "applet nao foi criado") {
        $classificacao = "CreateInstance falhou"
    }
    elseif ($log -match "boot=rodando") {
        $classificacao = "rodando"
    }
    else {
        # pega o ultimo frame reportado pra saber onde parou
        $lastFrame = ($log -split "`n" | Select-String "^\[INFO\] \[Zeebo\] frame " | Select-Object -Last 1)
        if ($lastFrame) {
            if ($lastFrame -match "boot=(\S+)") { $classificacao = "parado em boot=$($Matches[1])" }
            $detalhe = $lastFrame.ToString().Trim()
        }
        elseif ($log -match "retro_load_game") {
            $classificacao = "carregou mas sem frames"
        }
        else {
            $classificacao = "erro no load"
            $detalhe = ($log -split "`n" | Select-String "\[ERROR\]" | Select-Object -First 1).ToString().Trim()
        }
    }

    if ($log -match "fb\[0\]=(0x[0-9A-Fa-f]+)") { $fb0 = $Matches[1] }
    $instrMatches = [regex]::Matches($log, "instrucoes=(\d+)")
    if ($instrMatches.Count -gt 0) { $instrucoes = $instrMatches[$instrMatches.Count - 1].Groups[1].Value }

    $results += [pscustomobject]@{
        id = $d.Name
        arquivo = $modFile.Name
        classificacao = $classificacao
        detalhe = $detalhe
        fb0 = $fb0
        instrucoes = $instrucoes
    }
    Write-Host "[$i/$($dirs.Count)] $($d.Name) ($($modFile.Name)): $classificacao $detalhe" -ForegroundColor Cyan
}

$results | Export-Csv -Path $Out -NoTypeInformation -Encoding UTF8
Write-Host ""
Write-Host "=== RESUMO ===" -ForegroundColor Green
$results | Group-Object classificacao | Sort-Object Count -Descending | ForEach-Object {
    Write-Host ("{0,-30} {1}" -f $_.Name, $_.Count)
}
Write-Host ""
Write-Host "CSV salvo em: $Out" -ForegroundColor Green
