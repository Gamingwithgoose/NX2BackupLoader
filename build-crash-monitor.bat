@echo off
setlocal
cd /d "%~dp0"
if not defined DEVKITPRO set "DEVKITPRO=C:\devkitPro"
set "NX2_PROJECT_ROOT=%CD%"
"%DEVKITPRO%\msys2\usr\bin\bash.exe" -l < "%CD%\scripts\build-crash-monitor.sh"
if errorlevel 1 (echo [ERROR] Plugin build failed. & pause & exit /b 1)
echo Copy release\kaihen to SD root, then restart kaiHEN.
pause
