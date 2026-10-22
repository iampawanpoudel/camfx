# Downloads header/binary dependencies into third_party/ (git-ignored).
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$tp = Join-Path $root 'third_party'
New-Item -ItemType Directory -Force $tp | Out-Null

function Fetch($url, $name) {
    $dest = Join-Path $tp $name
    if (Test-Path $dest) { return }
    $zip = Join-Path $env:TEMP "$name.zip"
    $tmp = Join-Path $env:TEMP "$name-extract"
    Write-Host "Downloading $url"
    Invoke-WebRequest $url -OutFile $zip -UseBasicParsing
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
    Expand-Archive $zip $tmp
    Move-Item (Get-ChildItem $tmp | Select-Object -First 1).FullName $dest
    Remove-Item -Recurse -Force $tmp, $zip
}

Fetch 'https://github.com/microsoft/onnxruntime/releases/download/v1.30.0/onnxruntime-win-x64-1.30.0.zip' 'onnxruntime'
Fetch 'https://github.com/microsoft/wil/archive/refs/tags/v1.0.260126.7.zip' 'wil'
Write-Host "Dependencies ready in $tp"
