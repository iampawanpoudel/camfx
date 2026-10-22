@echo off
rem Builds x64 with the VS 2022 C++ toolchain. usage: build.cmd [Release|RelWithDebInfo|Debug]
setlocal
set CFG=%1
if "%CFG%"=="" set CFG=Release
set OUT=build
if not "%CFG%"=="Release" set OUT=build-%CFG%
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set VS=%%i
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0.."
if not exist third_party\onnxruntime powershell -ExecutionPolicy Bypass -File scripts\fetch-deps.ps1 || exit /b 1
cmake -S . -B %OUT% -G Ninja -DCMAKE_BUILD_TYPE=%CFG% || exit /b 1
cmake --build %OUT% || exit /b 1
ctest --test-dir %OUT% --output-on-failure
