@echo off
setlocal EnableExtensions
cd /d "%~dp0"
set "NX2_PROJECT_ROOT=%CD%"
if not defined DEVKITPRO set "DEVKITPRO=C:\devkitPro"
set "BASH=%DEVKITPRO%\msys2\usr\bin\bash.exe"
set "OUT=%~dp0release\switch\nx2-folder-exec\nx2-folder-exec.nro"
set "LOG=%~dp0build.log"
for /f "delims=" %%P in ('where python.exe 2^>nul') do if not defined NX2_PYTHON set "NX2_PYTHON=%%P"
set "RESULT=1"
> "%LOG%" echo NX2 Folder Execution Suite 0.3 build started
echo ===========================================================
echo NX2 Folder Execution Suite 0.3 - SWITCH 2 / libounce
echo ===========================================================
echo Source : %NX2_PROJECT_ROOT%
echo Log    : %LOG%
echo Output : %OUT%
echo.
if exist "%OUT%" del /f /q "%OUT%"
if exist "%OUT%" (
  echo [ERROR] Cannot delete old NRO. Close applications using it.
  goto finish
)
if not exist "%BASH%" (
  echo [ERROR] devkitPro MSYS2 bash missing: %BASH%
  goto finish
)
echo [BUILD] Running local devkitPro ounce-devkit build...
"%BASH%" -l < "%~dp0scripts\build-msys.sh" > "%LOG%" 2>&1
set "RESULT=%ERRORLEVEL%"
echo.
echo ---------------- BUILD OUTPUT ----------------
if exist "%LOG%" (type "%LOG%") else (echo [ERROR] No log file & for /f "delims=" %%P in ('where python.exe 2^>nul') do if not defined NX2_PYTHON set "NX2_PYTHON=%%P"
set "RESULT=1")
echo ---------------- END BUILD OUTPUT ------------
if not "%RESULT%"=="0" (
  echo [FAILED] Compilation exited with code %RESULT%.
  goto finish
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\verify-nro.ps1" -Path "%OUT%"
if errorlevel 1 (
  echo [FAILED] Switch 2 NRO verification failed.
  if exist "%OUT%" del /f /q "%OUT%"
  for /f "delims=" %%P in ('where python.exe 2^>nul') do if not defined NX2_PYTHON set "NX2_PYTHON=%%P"
set "RESULT=1"
  goto finish
)
echo.
echo [SUCCESS] Switch 2-format NRO built: %OUT%
echo WARNING: Not yet tested against your exact kaiHEN runtime.
set "RESULT=0"
:finish
if not "%RESULT%"=="0" if exist "%OUT%" del /f /q "%OUT%"
echo.
echo Build log: %LOG%
echo The window stays open until you press a key.
pause
exit /b %RESULT%
