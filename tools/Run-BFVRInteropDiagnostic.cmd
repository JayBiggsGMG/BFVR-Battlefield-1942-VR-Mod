@echo off
setlocal
cd /d "%~dp0"

if not exist "%~dp0BFVR.exe" (
  echo BFVR.exe was not found beside this diagnostic launcher.
  pause
  exit /b 2
)

if exist "%~dp0..\BFVR\UserConfig.txt" (
  copy /y "%~dp0..\BFVR\UserConfig.txt" "%~dp0UserConfig.txt" >nul
  if errorlevel 1 (
    echo Could not copy the existing BFVR settings into the diagnostic folder.
    pause
    exit /b 2
  )
)

echo Starting one-run BFVR interop diagnostics.
echo Play or reproduce the issue, then exit Battlefield 1942 normally.
echo BFVR will collect the diagnostic logs into a ZIP beside the game folder.
echo.
"%~dp0BFVR.exe" --interop-diagnostics
set "BFVR_EXIT_CODE=%ERRORLEVEL%"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Package-BFVRInteropDiagnostic.ps1" -GameRoot "%~dp0.."
if errorlevel 1 (
  echo Log packaging failed. The raw diagnostic logs remain in the BFVR folder.
  pause
  exit /b 1
)

echo.
echo Diagnostic run finished. Send BFVR-interop-diagnostic.zip to the BFVR maintainer.
pause
exit /b %BFVR_EXIT_CODE%
