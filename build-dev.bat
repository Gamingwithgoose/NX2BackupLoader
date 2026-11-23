@echo off
setlocal EnableExtensions
cd /d "%~dp0"
set "NX2_PROJECT_ROOT=%CD%"
if not defined DEVKITPRO set "DEVKITPRO=C:\devkitPro"
set "BASH=%DEVKITPRO%\msys2\usr\bin\bash.exe"
set "RESULT=1"
echo NX2BackupLoader PRIVATE developer diagnostic build
echo Public build.bat is unchanged. Plugin is NOT installed automatically.
set "PLUGIN_OUT=%CD%\release\developer\kaihen\plugins\nx2trace.nso"
if exist "%PLUGIN_OUT%" del /f /q "%PLUGIN_OUT%"
if exist "%PLUGIN_OUT%" (
 echo [ERROR] Failed to remove stale developer plugin. Build stopped.
 goto finish
)
if not exist "%BASH%" (
 echo [ERROR] devkitPro MSYS2 missing.
 goto finish
)
if not exist "%DEVKITPRO%\libounce\switch2_rules" (
 echo [ERROR] ounce-devkit missing. Run regular build.bat to install.
 goto finish
)
set "NX2_DEV_TRACE=1"
echo [BUILD] Instrumented BackupLoaderMKW.nro...
"%BASH%" -l < "%CD%\scripts\build-msys.sh"
if errorlevel 1 goto fail
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%CD%\scripts\verify-nro.ps1" -Path "%CD%\release\switch\BackupLoaderMKW\BackupLoaderMKW.nro"
if errorlevel 1 goto fail
echo [BUILD] Private nx2trace.nso...
"%BASH%" -l < "%CD%\scripts\build-dev-plugin-msys.sh"
if errorlevel 1 goto fail
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%CD%\scripts\verify-private-plugin.ps1" -Path "%PLUGIN_OUT%"
if errorlevel 1 goto fail
echo [SUCCESS] Developer NRO: release\switch\BackupLoaderMKW\BackupLoaderMKW.nro
echo [SUCCESS] Optional plugin: release\developer\kaihen\plugins\nx2trace.nso
echo [NOTE] Never copy release\developer to public distributions.
echo [NOTE] Plugin requires HEN bootstrap. Copy manually ONLY after reviewing README.
set "RESULT=0"
goto finish
:fail
if exist "%PLUGIN_OUT%" del /f /q "%PLUGIN_OUT%"
echo [ERROR] Developer build failed. Do not use older binaries as a new build.
:finish
echo.
pause
exit /b %RESULT%
