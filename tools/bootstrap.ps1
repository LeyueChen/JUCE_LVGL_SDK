param(
    [string]$Project,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [switch]$Check
)
$ErrorActionPreference = 'Stop'
$taskArguments = @('setup', '--config', $Configuration)
if ($Project) { $taskArguments += @('--project', $Project) }
if ($Check) { $taskArguments += '--check' }
& (Join-Path (Split-Path -Parent $PSScriptRoot) 'sdk.cmd') @taskArguments
exit $LASTEXITCODE
