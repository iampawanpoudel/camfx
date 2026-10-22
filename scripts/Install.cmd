@echo off
rem Double-click to install CamFX (asks for administrator rights).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
if errorlevel 1 (echo Install failed. & pause & exit /b 1)
start "" "%ProgramFiles%\CamFX\CamFX.exe"
