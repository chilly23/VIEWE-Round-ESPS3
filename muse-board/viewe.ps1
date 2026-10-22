param(
    [ValidateSet('build','flash','monitor','menuconfig','ports')]
    [string]$Action = 'build',
    [string]$Port = '',
    [switch]$Offline
)
$ErrorActionPreference = 'Stop'
$idf = $env:IDF_PATH
if (-not $idf) { $idf = Join-Path $env:USERPROFILE 'esp\esp-idf-v6.0.1' }
if (-not (Test-Path (Join-Path $idf 'tools\idf.py'))) {
    throw 'Install ESP-IDF v6.0.1 first, or set IDF_PATH to its directory.'
}
$pythonRoot = Join-Path $env:USERPROFILE '.espressif\python_env'
$python = Get-ChildItem -Path "$pythonRoot\idf6.0_py*_env\Scripts\python.exe" -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
if (-not $python) { throw 'ESP-IDF Python environment missing. Run the ESP-IDF installer.' }
$env:IDF_PATH = $idf
$env:PYTHONUTF8 = '1'
$exports = & $python (Join-Path $idf 'tools\idf_tools.py') export --format key-value
if ($LASTEXITCODE -ne 0) { throw 'ESP-IDF tools are not installed.' }
foreach ($line in $exports) {
    if ($line -match '^([A-Z_][A-Z_0-9]*)=(.*)$') {
        $name = $Matches[1]
        $value = $Matches[2].Replace('%PATH%', $env:Path).Replace('$PATH', $env:Path)
        [Environment]::SetEnvironmentVariable($name, $value, 'Process')
    }
}
$env:Path = (Split-Path $python) + ';' + $env:Path
$runnerArgs = @($Action)
if ($Offline) { $runnerArgs += '--offline' }
if ($Port) { $runnerArgs += @('--port', $Port) }
& $python (Join-Path $PSScriptRoot 'run-viewe.py') @runnerArgs
exit $LASTEXITCODE
