@echo off
REM EchoPod Simulator Launcher
REM Auto-detects project root. Edit MUSIC_PATH for your music folder.

setlocal enabledelayedexpansion

set "MUSIC_PATH=C:\Users\Public\Music"
if not "%~1"=="" set "MUSIC_PATH=%~1"

REM Auto-detect project root from this script location
set "TOOLSDIR=%~dp0"
set "PROJECT_ROOT=%TOOLSDIR%.."
cd /d "%PROJECT_ROOT%"

set "MSYS64=C:\msys64"
set "CLANG64_BIN=%MSYS64%\clang64\bin"
set "SYSTEM_DIR=%PROJECT_ROOT%\build\system"
set "BUILD_DIR=%PROJECT_ROOT%\build\dev"
set "SIM_EXE=%BUILD_DIR%\platform\sim\echo_sim.exe"

if not exist "%SIM_EXE%" (
    echo Error: Simulator not built.
    echo From MSYS2 CLANG64:
    echo   cmake -S . -B build/dev -DCMAKE_GENERATOR=Ninja -DCMAKE_C_COMPILER=clang
    echo   cmake --build build/dev
    exit /b 1
)

if not exist "%CLANG64_BIN%\clang.exe" (
    echo Error: MSYS2 CLANG64 not found at %CLANG64_BIN%
    echo Install from https://www.msys2.org/
    exit /b 1
)

set "PATH=%CLANG64_BIN%;%PATH%"
echo Running EchoPod simulator...
echo   Music: %MUSIC_PATH%
echo   System: %SYSTEM_DIR%

"%SIM_EXE%" --card "%MUSIC_PATH%" --system-dir "%SYSTEM_DIR%" %*
