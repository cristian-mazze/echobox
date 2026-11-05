# Windows Development Setup

## Quick Start

### Option 1: From MSYS2 CLANG64 Shell (Primary)

1. Open `C:\msys64\clang64.exe`
2. Run these commands:

```bash
cd /c/Users/Skemz/Dev/echobox
mkdir -p build/system
./build/dev/platform/sim/echo_sim.exe --card "/c/Users/Public/Music" --system-dir "build/system"
```

### Option 2: PowerShell (After Initial Build)

After building the project once from MSYS2, use PowerShell:

```powershell
# Run with default music folder (all users)
.\tools\run-sim.ps1

# Run with custom folder
.\tools\run-sim.ps1 -MusicPath "C:\MyMusic"

# Headless test (for CI/testing)
.\tools\run-sim.ps1 -Headless -Frames 120 -Screenshot result.bmp
```

### Option 3: Command Prompt (After Build)

```cmd
tools\run-sim.bat
```

## Building (Mandatory in MSYS2 CLANG64)

```bash
# 1. Open C:\msys64\clang64.exe
# 2. Run:
cd /c/Users/Skemz/Dev/echobox
cmake -S . -B build/dev -DCMAKE_GENERATOR=Ninja -DCMAKE_C_COMPILER=clang
cmake --build build/dev
ctest --preset dev
```

## Simulator Controls

| Key | Action |
|-----|--------|
| ↑ / ↓ | Volume / Navigate |
| Enter / Space | Play/Pause |
| M / Tab | Menu |
| Backspace / Esc | Back |
| P | Power |

## Headless Mode (No GUI)

```bash
./build/dev/platform/sim/echo_sim.exe \
    --headless \
    --frames 120 \
    --keys "down,down,play,back:long,menu" \
    --card "/c/Users/Public/Music" \
    --system-dir "build/system" \
    --screenshot result.bmp
```

## Path Notes for Windows

| MSYS2 Shell | Windows CMD/PowerShell |
|-------------|------------------------|
| `/c/Users/skemz/Music` | `C:\Users\skemz\Music` |
| `/c/Users/Public/Music` | `C:\Users\Public\Music` |

## Files

- `tools/run-sim.ps1` - PowerShell launcher script
- `tools/run-sim.bat` - Command Prompt launcher script
