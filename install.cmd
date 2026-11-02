@echo off
rem ============================================================
rem  install.cmd - install pet.exe to %LOCALAPPDATA%\DesktopPet
rem                 and optionally register auto-start (HKCU Run)
rem  Run build.bat first if you do not have pet.exe yet.
rem ============================================================
setlocal
cd /d "%~dp0"

set "SRC=%~dp0build\pet.exe"
if not exist "%SRC%" set "SRC=%~dp0pet.exe"
if not exist "%SRC%" (
  echo [ERR] pet.exe not found. Run build.bat first, or put pet.exe next to this script.
  exit /b 1
)

set "DIR=%LOCALAPPDATA%\DesktopPet"
if not exist "%DIR%" mkdir "%DIR%" >nul 2>&1
copy /y "%SRC%" "%DIR%\pet.exe" >nul
if errorlevel 1 (
  echo [ERR] copy failed.
  exit /b 1
)
echo [1/2] installed  -^>  "%DIR%\pet.exe"

set "AUTO="
choice /c YN /n /m "[2/2] Add to Windows start-up (auto-run on boot)? [Y/N] "
if errorlevel 2 goto :nostart
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v PetDinoPet /t REG_SZ /d "\"%DIR%\pet.exe\"" /f >nul
if errorlevel 1 (
  echo        [WARN] could not write the Run key, you can enable it from the tray menu instead.
) else (
  echo        start-up registered (HKCU Run -^> PetDinoPet)
)
goto :launch

:nostart
echo        start-up NOT enabled. You can turn it on later from the tray icon menu.

:launch
echo.
echo [DONE] starting the pet ...
start "" "%DIR%\pet.exe"
echo.
echo Uninstall later with uninstall.cmd
timeout /t 5 >nul
endlocal
