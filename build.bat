@echo off
setlocal EnableExtensions
cd /d "%~dp0"
set "NX2_PROJECT_ROOT=%CD%"
if not defined DEVKITPRO set "DEVKITPRO=C:\devkitPro"
set "BASH=%DEVKITPRO%\msys2\usr\bin\bash.exe"
if not exist "%BASH%" (
  echo [ERROR] MSYS2 bash not found: "%BASH%"
  echo Install devkitPro with the Nintendo Switch development packages.
  exit /b 1
)
echo NX2BackupLoader - local NRO build
echo Source: %NX2_PROJECT_ROOT%
"%BASH%" -l < "%~dp0scripts\build-msys.sh"
if errorlevel 1 (
  echo.
  echo [FAILED] No release was produced. Read the build errors above.
  exit /b 1
)
echo.
echo [OK] release\nx2backuploader.nro
exit /b 0
