@echo off
setlocal EnableExtensions
cd /d "%~dp0"
set "NX2_PROJECT_ROOT=%CD%"
if not defined DEVKITPRO set "DEVKITPRO=C:\devkitPro"
set "BASH=%DEVKITPRO%\msys2\usr\bin\bash.exe"
set "OUT=%CD%\release\switch\BackupLoaderMKW\BackupLoaderMKW.nro"
set "TOOLS=%CD%\tools"
set "RESULT=1"
echo BackupLoaderMKW - Nintendo Switch 2 build
if not exist "%TOOLS%" mkdir "%TOOLS%"
if not exist "%BASH%" (
 echo [ERROR] devkitPro MSYS2 missing. Install from https://devkitpro.org/wiki/Getting_Started
 goto finish
)
if not exist "%DEVKITPRO%\libounce\switch2_rules" (
 echo [SETUP] Downloading official ounce-devkit installer into tools...
 powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/ouncebrew/ounce-devkit/main/install.sh' -OutFile '%TOOLS%\ounce-install.sh'"
 if errorlevel 1 goto sdk_error
 "%BASH%" -l -c "export DEVKITPRO=$(cygpath -u '%DEVKITPRO%'); sh $(cygpath -u '%TOOLS%\ounce-install.sh')"
 if errorlevel 1 goto sdk_error
)
if not exist "%DEVKITPRO%\libounce\switch2_rules" goto sdk_error
echo [BUILD] Compiling BackupLoaderMKW...
"%BASH%" -l < "%CD%\scripts\build-msys.sh"
if errorlevel 1 goto build_error
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%CD%\scripts\verify-nro.ps1" -Path "%OUT%"
if errorlevel 1 goto build_error
echo [SUCCESS] %OUT%
echo [RUNTIME] release\switch\BackupLoaderMKW\runtime\BackupLoaderMKW-core.nro
echo Copy the contents of release\switch to your SD card root.
set "RESULT=0"
goto finish
:sdk_error
echo [ERROR] Switch 2 SDK missing or installation failed. No Switch 1 fallback.
goto finish
:build_error
echo [ERROR] Compilation or verification failed.
:finish
echo.
pause
exit /b %RESULT%
