@echo off
setlocal
cd /d "%~dp0"

where mingw32-make >nul 2>nul
if errorlevel 1 (
    echo mingw32-make not found. Install MinGW-w64 / MSYS2 and add it to PATH.
    exit /b 1
)

if not exist "..\raylib-6.2\src\libraylib.a" (
    echo Building raylib...
    pushd ..\raylib-6.2\src
    mingw32-make PLATFORM=PLATFORM_DESKTOP RAYLIB_LIBTYPE=STATIC
    if errorlevel 1 (popd & exit /b 1)
    popd
)

mingw32-make -f Makefile.win
if errorlevel 1 exit /b 1

echo Done: displacebox.exe
