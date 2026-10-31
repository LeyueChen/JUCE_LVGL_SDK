param([Parameter(ValueFromRemainingArguments=$true)][string[]]$SdkArguments)
$ErrorActionPreference = 'Stop'
& (Join-Path (Split-Path -Parent $PSScriptRoot) 'sdk.cmd') @SdkArguments
exit $LASTEXITCODE
