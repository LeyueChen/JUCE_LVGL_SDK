param([Parameter(ValueFromRemainingArguments=$true)][string[]]$SdkArguments)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskCandidates = @(
    (Join-Path $taskRoot '.venv\Scripts\python.exe'),
    $env:SDK_PYTHON,
    (Join-Path $env:USERPROFILE '.local\bin\python3.14.exe')
)
$taskPython = $taskCandidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
if (-not $taskPython) {
    $taskPythonCommand = Get-Command python3,python -ErrorAction SilentlyContinue | Where-Object { $_.Source -notlike '*WindowsApps*' } | Select-Object -First 1
    if ($taskPythonCommand) { $taskPython = $taskPythonCommand.Source }
}
if (-not $taskPython) { throw 'Python 3.11+ required. Set SDK_PYTHON to its executable path.' }
& $taskPython (Join-Path $PSScriptRoot 'sdk.py') @SdkArguments
exit $LASTEXITCODE
