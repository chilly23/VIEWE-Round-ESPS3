$ErrorActionPreference = 'Stop'
$python = Get-ChildItem -Path "$env:USERPROFILE\.espressif\python_env\idf6.0_py*_env\Scripts\python.exe" -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
if (!$python) { throw 'The ESP-IDF Python environment is missing on this PC.' }
& $python (Join-Path $PSScriptRoot 'flash.py')
exit $LASTEXITCODE
