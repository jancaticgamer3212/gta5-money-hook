@echo off
setlocal
cd /d "%~dp0"

set ZIG=%LOCALAPPDATA%\qoder-toolchains\zig-x86_64-windows-0.17.1\zig.exe
if not exist "%ZIG%" (
  echo Zig not found: %ZIG%
  exit /b 1
)

if not exist dist mkdir dist

"%ZIG%" cc -target x86_64-windows-gnu -shared -O2 -Wall -o dist\dinput8.dll src\dinput8_proxy.c src\dinput8.def -lkernel32 -luser32
if errorlevel 1 (
  echo Build failed.
  exit /b 1
)

copy /y config\GTA5Money.ini dist\GTA5Money.ini >nul
echo Built dist\dinput8.dll
