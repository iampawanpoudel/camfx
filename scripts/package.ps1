# Builds Release and zips everything another PC needs into dist\CamFX-win-x64.zip.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
& cmd /c "`"$PSScriptRoot\build.cmd`" Release"
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }

$stage = Join-Path $root 'dist\CamFX'
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null
foreach ($f in 'CamFX.exe', 'CamFXCamera.dll', 'onnxruntime.dll', 'person_segmenter.onnx') { Copy-Item "$root\build\$f" $stage }
foreach ($f in 'install.ps1', 'Install.cmd', 'Uninstall.cmd') { Copy-Item "$PSScriptRoot\$f" $stage }
Copy-Item "$root\README.md", "$root\LICENSE" $stage
# Licenses of the bundled third-party parts
Copy-Item "$root\third_party\onnxruntime\LICENSE" "$stage\LICENSE-onnxruntime.txt"
Copy-Item "$root\vcam\LICENSE-Microsoft.txt", "$root\models\LICENSE-PP-HumanSeg.txt" $stage

$zip = Join-Path $root 'dist\CamFX-win-x64.zip'
Compress-Archive "$stage\*" $zip -Force
Write-Host "Created $zip"
