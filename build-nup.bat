@echo off
setlocal EnableExtensions
cd /d "%~dp0"
echo ==========================================================
echo BackupLoaderMKW - Batch NUP Builder
echo ==========================================================
echo Input : %CD%\extracted
echo Output: %CD%\nups
echo.
where py >nul 2>&1
if errorlevel 1 (
  echo [ERROR] Python launcher "py" not found. Install Python 3 for Windows.
  goto failed
)
py -c "import nup.pack, zstandard" >nul 2>&1
if errorlevel 1 (
  echo [SETUP] Installing upstream NUP packer and zstandard...
  py -m pip install zstandard "git+https://github.com/a55uka/nup.git@meow#subdirectory=tools/nup"
  if errorlevel 1 (
    echo [ERROR] Could not install required NUP packer dependencies.
    goto failed
  )
)
if not exist "extracted" mkdir "extracted"
if not exist "nups" mkdir "nups"
py "tools\build_nups.py"
if errorlevel 1 goto failed
echo.
echo [DONE] Generated archives are in %CD%\nups
echo Copy them to SD:\games to launch with BackupLoaderMKW.
pause
exit /b 0
:failed
echo.
echo [FAILED] No existing NUP files were overwritten.
pause
exit /b 1
