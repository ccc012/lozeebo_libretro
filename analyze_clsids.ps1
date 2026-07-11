# Scan todos os MODs e MIFs para CLSIDs usados
# Identifica potenciais bloqueadores

$rombase = "C:\Users\Lucas\Downloads\zeebo-romset-and-devtools\Zeebo\Zeebo\Zeebo Game & App Compilation - OpenZeebo\274804"

Write-Host "=== ANALISANDO CLSIDs DE 68 JOGOS ===" -ForegroundColor Cyan
Write-Host ""

# MIFs contêm metadados legíveis em texto
$mifFiles = Get-ChildItem "$rombase\mif" -Filter "*.mif" -File
$modDirs = Get-ChildItem "$rombase\mod" -Directory

Write-Host "MIFs encontrados: $($mifFiles.Count)" -ForegroundColor Green
Write-Host "MODs encontrados: $($modDirs.Count)" -ForegroundColor Green
Write-Host ""

# Tentar ler CLSIDs de MIFs (formato texto)
Write-Host "--- CLSIDs encontrados em MIFs ---" -ForegroundColor Yellow
foreach ($mif in $mifFiles | Select-Object -First 5) {
    Write-Host "Lendo $($mif.Name)..."
    $content = Get-Content $mif.FullName -Raw -ErrorAction SilentlyContinue
    if ($content) {
        # MIFs contêm metadados legíveis
        $lines = $content -split "`n" | Select-Object -First 20
        $lines | ForEach-Object { if ($_ -match "clsid|CLSID|0x[0-9A-Fa-f]+") { Write-Host "  $_" } }
    }
}

Write-Host ""
Write-Host "--- MODs (binários) ---" -ForegroundColor Yellow
Write-Host "Scanning $($modDirs.Count) MODs..."
Write-Host "Espaço total: ~2.1 GB"
Write-Host ""

# Primeiros 10 jogos
$games = @(
    @{ id = "276212"; name = "Pac-Mania" },
    @{ id = "277229"; name = "Family Pack" },
    @{ id = "274754"; name = "Double Dragon" },
    @{ id = "279382"; name = "Zeeboids" },
    @{ id = "274214"; name = "Crash Bandicoot" },
    @{ id = "274802"; name = "Quake" },
    @{ id = "276153"; name = "Quake II" },
    @{ id = "274803"; name = "FIFA 09" },
    @{ id = "276675"; name = "Resident Evil 4" },
    @{ id = "279125"; name = "Super BurgerTime" }
)

foreach ($game in $games) {
    $mifPath = "$rombase\mif\$($game.id).mif"
    $modPath = "$rombase\mod\$($game.id)"

    if (Test-Path $mifPath) {
        $size = (Get-Item $mifPath).Length
        Write-Host "$($game.id) - $($game.name): $($size) bytes"
    } elseif (Test-Path $modPath) {
        $modFiles = Get-ChildItem $modPath -File
        $totalSize = ($modFiles | Measure-Object -Sum Length).Sum
        Write-Host "$($game.id) - $($game.name): $($modFiles.Count) MODs, $($totalSize) bytes total"
    }
}

Write-Host ""
Write-Host "=== PRÓXIMOS PASSOS ===" -ForegroundColor Cyan
Write-Host "1. Testar Tier 1 jogos em RetroArch"
Write-Host "2. Coletar logs de erro para CLSIDs desconhecidos"
Write-Host "3. Implementar stubs específicos conforme necessário"
