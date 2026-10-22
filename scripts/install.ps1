# Installs CamFX from build\ into Program Files (self-elevates). Use -Uninstall to remove.
param([switch]$Uninstall)
$ErrorActionPreference = 'Stop'

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) {
    $argsList = @('-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"")
    if ($Uninstall) { $argsList += '-Uninstall' }
    $p = Start-Process powershell -Verb RunAs -ArgumentList $argsList -Wait -PassThru
    exit $p.ExitCode
}

$dest = Join-Path $env:ProgramFiles 'CamFX'
$data = Join-Path $env:ProgramData 'CamFX'
$lnk = Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'CamFX.lnk'
$dll = Join-Path $dest 'CamFXCamera.dll'

Get-Process CamFX -ErrorAction SilentlyContinue | Stop-Process -Force
# Frame Server keeps an old CamFXCamera.dll loaded (even when renamed), so restart it to release or reload it.
# It starts again on demand; this drops any camera stream open right now.
& taskkill.exe /F /FI "SERVICES eq FrameServer" | Out-Null

if ($Uninstall) {
    if (Test-Path "$dest\CamFX.exe") { & "$dest\CamFX.exe" --remove | Out-Null }
    if (Test-Path $dll) { & regsvr32.exe /s /u $dll; Start-Sleep -Milliseconds 500 }
    Remove-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name CamFX -ErrorAction SilentlyContinue
    Remove-Item $lnk -ErrorAction SilentlyContinue
    # Frame Server may still hold the DLL; it is released once no app uses the camera.
    Remove-Item -Recurse -Force $dest -ErrorAction SilentlyContinue
    Write-Host "CamFX removed. Settings kept in $data."
    exit 0
}

# Release zip: files sit next to this script. Source checkout: they are in build\.
$build = if (Test-Path (Join-Path $PSScriptRoot 'CamFX.exe')) { $PSScriptRoot } else { Join-Path (Split-Path $PSScriptRoot -Parent) 'build' }
New-Item -ItemType Directory -Force $dest, $data | Out-Null
foreach ($f in 'CamFX.exe', 'CamFXCamera.dll', 'onnxruntime.dll', 'person_segmenter.onnx') {
    $old = Join-Path $dest $f
    # A camera app may have restarted Frame Server already; a loaded file can still be renamed aside.
    if (Test-Path $old) { Rename-Item $old "$f.$([guid]::NewGuid().ToString('N')).old" -ErrorAction SilentlyContinue }
    Copy-Item (Join-Path $build $f) $dest -Force
}
Get-ChildItem $dest -Filter '*.old' | Remove-Item -Force -ErrorAction SilentlyContinue
Get-ChildItem $dest | Unblock-File # drop the downloaded-from-internet mark from release zips
& regsvr32.exe /s $dll
if ($LASTEXITCODE -ne 0) { throw "regsvr32 failed ($LASTEXITCODE)" }

# Frame Server runs the media source as LocalService (S-1-5-19); the signed-in (interactive) user edits settings.
& icacls.exe $data /grant '*S-1-5-19:(OI)(CI)RX' '*S-1-5-4:(OI)(CI)M' | Out-Null

$shell = New-Object -ComObject WScript.Shell
$s = $shell.CreateShortcut($lnk)
$s.TargetPath = "$dest\CamFX.exe"
$s.Save()

Write-Host "CamFX installed to $dest"
