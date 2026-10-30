$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskPython = $env:SDK_PYTHON
if (-not $taskPython) {
    $taskPython = Join-Path $env:USERPROFILE '.local\bin\python3.14.exe'
    if (-not (Test-Path -LiteralPath $taskPython)) {
        $taskCommand = Get-Command python3,python -ErrorAction SilentlyContinue | Where-Object { $_.Source -notlike '*WindowsApps*' } | Select-Object -First 1
        if (-not $taskCommand) { throw 'Install Python 3.11+ or set SDK_PYTHON to its executable.' }
        $taskPython = $taskCommand.Source
    }
}
Push-Location $taskRoot
try {
    $env:UV_CACHE_DIR = Join-Path $taskRoot '.cache\uv'
    $env:UV_LINK_MODE = 'copy'
    $taskUv = Get-Command uv -ErrorAction SilentlyContinue
    if ($taskUv) {
        if (-not (Test-Path -LiteralPath '.venv\Scripts\python.exe')) {
            & $taskUv.Source venv .venv --python $taskPython
            if ($LASTEXITCODE) { throw 'Virtual environment creation failed.' }
        }
        & $taskUv.Source pip install --python .venv\Scripts\python.exe --require-hashes -r tools\requirements.txt
    } else {
        if (-not (Test-Path -LiteralPath '.venv\Scripts\python.exe')) { & $taskPython -m venv .venv }
        & .venv\Scripts\python.exe -m pip install --cache-dir .cache\pip --require-hashes -r tools\requirements.txt
    }
    if ($LASTEXITCODE) { throw 'Host dependency installation failed.' }
    & .\tools\sdk.ps1 doctor
    if ($LASTEXITCODE) { throw 'Toolchain verification failed.' }
} finally { Pop-Location }
