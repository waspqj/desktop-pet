@echo off
rem ============================================================
rem  uninstall.cmd - stop the pet, remove auto-start, delete files
rem  Safe to run more than once.
rem ============================================================
setlocal

echo [1/4] stopping pet ...
taskkill /f /im pet.exe >nul 2>&1

echo [2/4] removing start-up entry ...
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v PetDinoPet /f >nul 2>&1

echo [3/4] removing installed files ...
if exist "%LOCALAPPDATA%\DesktopPet" rmdir /s /q "%LOCALAPPDATA%\DesktopPet" >nul 2>&1
if exist "%APPDATA%\PetDinoPet" rmdir /s /q "%APPDATA%\PetDinoPet" >nul 2>&1

echo [4/4] done.
echo.
echo The source folder (where this script lives) was kept. Delete it manually if you want.
timeout /t 5 >nul
endlocal
