@echo off
rem Double-click to remove CamFX (asks for administrator rights).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" -Uninstall
pause
