$ErrorActionPreference = 'Stop'
Write-Host 'Connect your VIEWE board with a USB DATA cable.'
# A child process is used because viewe.ps1 exits with the tool's status.
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'viewe.ps1') ports
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$viewePort = (Read-Host 'Enter its USB COM port, for example COM5').Trim()
if ($viewePort -notmatch '^COM[0-9]+$') { throw 'Enter a COM port from the list.' }
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'viewe.ps1') flash -Port $viewePort
exit $LASTEXITCODE
