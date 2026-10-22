$ErrorActionPreference = 'Stop'
$secret = Read-Host 'Paste your Muse SDK token (hidden)' -AsSecureString
$pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secret)
try {
    $token = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer).Trim()
    if ($token -notmatch '^mgst_[A-Za-z0-9._~-]+$') {
        throw 'Expected a Muse SDK token beginning with mgst_. Nothing saved.'
    }
    [IO.File]::WriteAllText((Join-Path $PSScriptRoot '.sdk-token'), $token, [Text.UTF8Encoding]::new($false))
    Write-Host 'Token saved locally. It is excluded from Git. Do not share this file or your built firmware.'
} finally {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer)
    $token = $null
}
