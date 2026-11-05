# Windows Development Setup

## Prerequisites

- **Python 3.10+**: Required for the build tools and CLI
- **MSYS2 with Clang**: Install from https://www.msys2.org/
  - Run: `pacman -S mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-compiler-rt`
  - This provides clang, lldb, and SDL2 for the simulator

## Quick Start: Simulator CLI

The `sim` script is the recommended way to build and run the simulator:

```powershell
# Setup the development environment (installs Python venv, cmake, ninja, clang)
.\sim build

# Run the simulator with your music library
.\sim run --card "D:\MyMusic"

# Run in headless mode (no GUI)
.\sim run --headless --frames 120 --keys "down,down,play" --screenshot result.bmp

# Watch mode: rebuild and relaunch on file changes
.\sim watch
```

The `sim.cmd` launcher is automatically found from PowerShell and CMD:

```cmd
sim build
sim run
```

## Build

Use the `dev` preset for development:

```bash
# MSYS2 CLANG64 Shell (recommended for initial setup)
mkdir build/system
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Or use the CLI tools:

```powershell
.\sim setup
.\sim build
.\sim test
```

## Simulator Controls

| Key | Action |
|-----|--------|
| ↑ / ↓ | Volume / Navigate |
| Enter / Space | Play/Pause |
| M / Tab | Menu |
| Backspace / Esc | Back |
| P | Power |

## PATH Notes for Windows

| MSYS2 Shell | Windows CMD/PowerShell |
|-------------|------------------------|
| `/c/Users/username/Music` | `C:\Users\username\Music` |
| `/c/Users/Public/Music` | `C:\Users\Public\Music` |

## Configuration

Create `sim.local.json` in the project root to store your preferences:

```json
{
    "card": "D:/Music",
    "scale": 2,
    "system_dir": "build/system"
}
```

Or use the CLI:

```powershell
.\sim config card "D:\Music"
.\sim config scale 2
```

## Python Environment

Install Python dependencies:

```bash
python3 -m venv .venv
.venv\Scripts\activate
pip install -r tools/requirements.txt
```

## Debugging

Launch the simulator under the debugger:

```powershell
.\sim debug
```

This uses lldb with the same arguments as `sim run`.

## Files

- `sim` - Unix shell launcher script
- `sim.cmd` - Windows CMD/PowerShell launcher script  
- `tools/sim.py` - Python CLI implementation
- `tools/tests/test_sim.py` - CLI tests
- `cmake/sdl2.cmake` - SDL2 configuration module
- `pyproject.toml` - Python project configuration