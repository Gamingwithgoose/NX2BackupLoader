@echo off
setlocal EnableExtensions
cd /d "%~dp0"
set "NX2_PROJECT_ROOT=%CD%"
if not defined DEVKITPRO set "DEVKITPRO=C:\devkitPro"
set "BASH=%DEVKITPRO%\msys2\usr\bin\bash.exe"
set "OUT=%~dp0release\nx2backuploader.nro"
set "LOG=%~dp0build.log"
set "RESULT=1"

echo ===========================================================
echo   NX2BackupLoader - local Switch 2 NRO build
echo ===========================================================
echo Source : %NX2_PROJECT_ROOT%
echo Log    : %LOG%
echo Output : %OUT%
echo.

if not exist "%BASH%" (
  echo [ERROR] devkitPro MSYS2 bash not found: "%BASH%"
  echo Install the devkitPro Nintendo Switch development packages.
  goto finish
)

rem Always delete the previous release before building, so an old NRO
rem cannot be mistaken for a successful new build.
if exist "%OUT%" del /f /q "%OUT%"
if exist "%OUT%" (
  echo [ERROR] Cannot delete old release NRO; close programs using it.
  goto finish
)

echo [BUILD] Running devkitPro build. All output is saved to build.log.
echo.
"%BASH%" -l < "%~dp0scripts\build-msys.sh" > "%LOG%" 2>&1
set "RESULT=%ERRORLEVEL%"

echo ---------------- BUILD OUTPUT ----------------
if exist "%LOG%" (
  type "%LOG%"
) else (
  echo [ERROR] Build log was not created.
  set "RESULT=1"
)
echo ---------------- END BUILD OUTPUT ------------

if not "%RESULT%"=="0" (
  echo.
  echo [FAILED] Build exited with error code %RESULT%.
  goto finish
)
if not exist "%OUT%" (
  echo [ERROR] Build reported success but release NRO is missing.
  set "RESULT=1"
  goto finish
)

rem NRO files contain the ASCII NRO0 signature at file offset 0x10.
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; $p=$env:OUT; $f=[IO.File]::OpenRead($p); try { if($f.Length -lt 0x80){throw 'Output is too small to be a valid NRO'}; $f.Position=0x10; $buf=New-Object byte[] 4; if($f.Read($buf,0,4) -ne 4 -or [Text.Encoding]::ASCII.GetString($buf) -ne 'NRO0'){throw 'NRO0 signature missing at offset 0x10'}; Write-Host ('[OK] NRO header present; size: ' + $f.Length + ' bytes') } finally { $f.Dispose() }"
if errorlevel 1 (
  echo [ERROR] NRO header verification failed.
  set "RESULT=1"
  goto finish
)

echo.
echo [SUCCESS] NRO built and basic header verified.
echo %OUT%
echo Note: Header verification does not prove Switch 2 runtime compatibility.
set "RESULT=0"

:finish
echo.
echo Build log: %LOG%
echo The window will remain open until you press a key.
pause
exit /b %RESULT%
