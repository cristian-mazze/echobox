# EchoPod Simulator Launcher
# Any user can edit MusicPath below to point to their music folder

param(
    [string]$MusicPath = "C:\Users\Skemz\Music",
    [string]$SystemDir = "C:\Users\Skemz\Dev\echobox\build\system",
    [switch]$Headless,
    [int]$Frames = 0,
    [string]$Keys = "",
    [string]$Screenshot = ""
)

$ErrorActionPreference = "Stop"

$CLANG64_BIN = "C:\msys64\clang64\bin"
$BUILD_DIR = "C:\Users\Skemz\Dev\echobox\build\dev"
$SIM_EXE = Join-Path $BUILD_DIR "platform\sim\echo_sim.exe"

if (-not (Test-Path $SIM_EXE)) {
    Write-Error "Simulator not found. Run from MSYS2 CLANG64:"
    Write-Error "  cmake -S . -B build/dev -DCMAKE_GENERATOR=Ninja -DCMAKE_C_COMPILER=clang"
    Write-Error "  cmake --build build/dev"
    exit 1
}

if (-not (Test-Path "$CLANG64_BIN\clang.exe")) {
    Write-Error "MSYS2 CLANG64 not found at $CLANG64_BIN"
    exit 1
}

$Args = @()
$Args += "--card"
$Args += $MusicPath
$Args += "--system-dir"
$Args += $SystemDir

if ($Headless) {
    $Args += "--headless"
    if ($Frames -gt 0) { $Args += "--frames"; $Args += $Frames }
    if ($Keys) { $Args += "--keys"; $Args += $Keys }
    if ($Screenshot) { $Args += "--screenshot"; $Args += $Screenshot }
}

$Env:PATH = "$CLANG64_BIN;$Env:PATH"

Write-Host "Running EchoPod simulator..."
Write-Host "  Music: $MusicPath"
Write-Host "  System: $SystemDir"

& $SIM_EXE $Args
