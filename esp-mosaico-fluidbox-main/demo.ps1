param([ValidateSet('build','flash','monitor')][string]$Action='build',[string]$Port='')
$ErrorActionPreference='Stop'
$idf=Join-Path $env:USERPROFILE 'esp\esp-idf-v6.0.1'
$python=Join-Path $env:USERPROFILE '.espressif\python_env\idf6.0_py3.13_env\Scripts\python.exe'
if(-not(Test-Path -LiteralPath $python)){throw 'ESP-IDF 6.0.1 Python is missing.'}
$env:IDF_PATH=$idf
$env:PYTHONUTF8='1'
$exports=& $python (Join-Path $idf 'tools\idf_tools.py') export --format key-value
if($LASTEXITCODE -ne 0){throw 'ESP-IDF tools could not be loaded.'}
foreach($line in $exports){if($line -match '^([A-Z_][A-Z_0-9]*)=(.*)$'){
 [Environment]::SetEnvironmentVariable($Matches[1],$Matches[2].Replace('%PATH%',$env:Path).Replace('$PATH',$env:Path),'Process')
}}
$env:Path=(Split-Path $python)+';'+$env:Path
if($Action -ne 'build' -and -not $Port){
 $devices=@(Get-CimInstance Win32_SerialPort | Where-Object {$_.PNPDeviceID -match 'VID_303A&PID_1001'})
 if($devices.Count -eq 1){$Port=$devices[0].DeviceID}
 else {Get-CimInstance Win32_SerialPort | Select-Object DeviceID,Description | Format-Table; $Port=Read-Host 'Enter the ESP32 COM port'}
 if($Port -notmatch '^COM\d+$'){throw 'Invalid COM port.'}
}
if($Action -eq 'flash' -and (Test-Path (Join-Path $PSScriptRoot 'firmware\manifest.json'))){
 & $python (Join-Path $PSScriptRoot 'flash-demo.py') $Port
 if($LASTEXITCODE -ne 0){throw 'Upload failed. Hold BOOT, tap RST, release BOOT and retry.'}
 exit
}
Push-Location $PSScriptRoot
try {
 $idfArgs=@((Join-Path $idf 'tools\idf.py'),'-B','build','-DIDF_TARGET=esp32s3','-DCMAKE_JOB_POOLS=compile=2;link=1','-DCMAKE_JOB_POOL_COMPILE=compile','-DCMAKE_JOB_POOL_LINK=link')
 if($Port){$idfArgs+=@('-p',$Port)}
 $idfArgs+=$Action
 & $python @idfArgs
 if($LASTEXITCODE -ne 0){throw "ESP-IDF $Action failed. Read the first error above."}
} finally {Pop-Location}

if($Action -eq 'build'){
 & $python (Join-Path $PSScriptRoot '..\VIEWE-Demos\package-firmware.py') (Split-Path $PSScriptRoot -Leaf)
 if($LASTEXITCODE -ne 0){throw 'Build passed but firmware packaging failed.'}
}
exit 0
