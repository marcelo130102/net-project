$ErrorActionPreference = "Stop"

$Distro = "Ubuntu"

# Carpeta donde está este script = raíz del repo
$RepoWin = Split-Path -Parent $MyInvocation.MyCommand.Path

function Convert-ToWslPath($winPath) {
    $full = (Resolve-Path $winPath).Path

    if ($full -match '^([A-Za-z]):\\(.*)$') {
        $drive = $matches[1].ToLower()
        $rest = $matches[2] -replace '\\', '/'
        return "/mnt/$drive/$rest"
    }

    throw "No pude convertir la ruta Windows a WSL: $full"
}

function BashQuote($text) {
    return "'" + ($text -replace "'", "'\''") + "'"
}

function Write-LfFile($path, $content) {
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    $normalized = $content -replace "`r`n", "`n"
    $normalized = $normalized -replace "`r", "`n"
    [System.IO.File]::WriteAllText($path, $normalized, $utf8NoBom)
}

function Run-WslScript($scriptWinPath) {
    $scriptWslPath = Convert-ToWslPath $scriptWinPath
    wsl -d $Distro -- bash $scriptWslPath
}

function Start-WslScriptConsole($title, $scriptWinPath) {
    $scriptWslPath = Convert-ToWslPath $scriptWinPath
    $safeTitle = $title -replace "'", "''"

    $psCommand = "`$Host.UI.RawUI.WindowTitle = '$safeTitle'; wsl -d $Distro -- bash '$scriptWslPath'"

    Start-Process powershell.exe -ArgumentList @(
        "-NoExit",
        "-Command",
        $psCommand
    )
}

$RepoWsl = Convert-ToWslPath $RepoWin

Write-Host "Repo Windows: $RepoWin"
Write-Host "Repo WSL:     $RepoWsl"
Write-Host ""

$slavesInput = Read-Host "Cuantos esclavos quieres usar? No cuentes al master"
$slaves = [int]$slavesInput

$dataset = Read-Host "Nombre del dataset principal. Enter = Dataset of Diabetes.csv"
if ([string]::IsNullOrWhiteSpace($dataset)) {
    $dataset = "Dataset of Diabetes.csv"
}

$totalParts = $slaves + 1

Write-Host ""
Write-Host "Preparando demo con $slaves slaves + 1 master..."
Write-Host "Total de terminales que se abriran: $totalParts"
Write-Host "Dataset: $dataset"
Write-Host ""

$LauncherDir = Join-Path $RepoWin ".demo_launcher"

if (!(Test-Path $LauncherDir)) {
    New-Item -ItemType Directory -Path $LauncherDir | Out-Null
}

$repoQ = BashQuote $RepoWsl
$datasetQ = BashQuote $dataset

# Script de preparación
$setupScript = @"
#!/usr/bin/env bash
set -e

cd $repoQ

echo "[SETUP] Entrando al repo..."
echo "[SETUP] Repo: $(pwd)"

if [ ! -d .venv ]; then
    echo "[SETUP] Creando entorno virtual..."
    python3 -m venv .venv
fi

echo "[SETUP] Activando entorno virtual..."
source .venv/bin/activate

echo "[SETUP] Instalando/verificando dependencias..."
python -m pip install -q --upgrade pip setuptools wheel pybind11 numpy pandas matplotlib scikit-learn torch

if ! ls cpp_python_example/modulo*.so >/dev/null 2>&1 || ! ls cpp_python_example/calculator*.so >/dev/null 2>&1; then
    echo "[SETUP] Compilando bindings C++..."
    cd cpp_python_example
    python setup.py build_ext --inplace
    cd ..
else
    echo "[SETUP] Bindings C++ ya compilados."
fi

echo "[SETUP] Dividiendo dataset en $totalParts partes..."
python dataset_splitter.py $datasetQ $totalParts

echo "[SETUP] Preparación terminada."
"@

$setupPath = Join-Path $LauncherDir "setup.sh"
Write-LfFile $setupPath $setupScript

Run-WslScript $setupPath

Write-Host ""
Write-Host "Preparacion lista. Creando scripts de terminales..."
Write-Host ""

# Script del master
$masterScript = @"
#!/usr/bin/env bash
set -e

cd $repoQ
source .venv/bin/activate

echo "[MASTER] Ejecutando master con $slaves slaves..."
echo "[MASTER] Archivo: diabetes_master.csv"
echo ""

python -u basicClasificacion_master.py $slaves diabetes_master.csv

echo ""
echo "[MASTER TERMINADO O DETENIDO]"
exec bash
"@

$masterPath = Join-Path $LauncherDir "master.sh"
Write-LfFile $masterPath $masterScript

# Scripts de slaves
for ($i = 0; $i -lt $slaves; $i++) {
    $slaveFileIndex = $i + 1
    $slaveScript = @"
#!/usr/bin/env bash
set -e

cd $repoQ
source .venv/bin/activate

echo "[SLAVE $i] Ejecutando slave $i..."
echo "[SLAVE $i] Archivo: diabetes_slave_$slaveFileIndex.csv"
echo ""

python -u basicClasificacion_slave.py $i diabetes_slave_$slaveFileIndex.csv

echo ""
echo "[SLAVE $i TERMINADO O DETENIDO]"
exec bash
"@

    $slavePath = Join-Path $LauncherDir "slave_$i.sh"
    Write-LfFile $slavePath $slaveScript
}

Write-Host "Abriendo consolas..."
Write-Host ""

Start-WslScriptConsole "MASTER" $masterPath

Start-Sleep -Seconds 3

for ($i = 0; $i -lt $slaves; $i++) {
    $slavePath = Join-Path $LauncherDir "slave_$i.sh"
    Start-WslScriptConsole "SLAVE $i" $slavePath
    Start-Sleep -Milliseconds 900
}

Write-Host "Listo. Se abrieron las consolas de la demo."
Write-Host ""
Write-Host "Recuerda: si el requisito es exactamente 5 terminales visibles, usa 4 esclavos."