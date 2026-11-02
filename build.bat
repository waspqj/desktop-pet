@echo off
rem ============================================================
rem  build.bat - build pet.exe from source (MinGW-w64 / GCC)
rem  Usage:  double-click, or run in a terminal
rem  Output: build\pet.exe
rem ============================================================
setlocal
cd /d "%~dp0"

set "CC="
set "RC="
if exist "D:\mingw64\bin\gcc.exe" (
  set "CC=D:\mingw64\bin\gcc.exe"
  set "RC=D:\mingw64\bin\windres.exe"
) else (
  for /f "delims=" %%i in ('where gcc 2^>nul') do if not defined CC set "CC=%%i"
  for /f "delims=" %%i in ('where windres 2^>nul') do if not defined RC set "RC=%%i"
)

if not defined CC goto :notools
if not defined RC goto :notools

if not exist build mkdir build

echo [1/2] compiling resources ...
"%RC%" -i pet.rc -o build\pet_res.o
if errorlevel 1 goto :err
echo [2/2] compiling pet.c ...
"%CC%" pet.c build\pet_res.o -o build\pet.exe -mwindows -static -s -Os -finput-charset=utf-8 -lgdi32 -luser32 -lole32 -lshell32 -ladvapi32 -limm32
if errorlevel 1 goto :err

echo.
echo [OK] build\pet.exe
goto :eof

:notools
echo [FAIL] MinGW-w64 not found.
echo        Install it (https://www.mingw-w64.org/downloads/) and make sure
echo        "gcc" and "windres" are in PATH, then run build.bat again.
exit /b 1

:err
echo [FAIL] build error, see the message above.
exit /b 1

:eof
endlocal
