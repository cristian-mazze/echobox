# EchoPod Development Rules

This project is EchoPod, replacement firmware for the Snowsky (FiiO) Echo Mini music player. It's a freestanding C11 project with strict code standards. ALWAYS follow these rules when generating code.

## CRITICAL: NO COMMENTS IN CODE

**NEVER** write comments in C, CMake, or Python files. Names and small functions carry the intent. Prose belongs in `docs/`.

## Naming Standards (MANDATORY)

| Kind | Style | Example |
|---|---|---|
| Types | `PascalCase` | `KeyTracker`, `FrameStats` |
| Functions/Variables | `snake_case` | `key_tracker_update` |
| Constants/Enum Values | `UPPER_SNAKE_CASE` | `KEY_GESTURE_PRESS` |
| Files | `snake_case` | `key_tracker.c` |

## Function Rules (NON-NEGOTIABLE)

- Do one thing
- Stay short (under 20 lines preferred)  
- Maximum 4 parameters (group extras in structs)
- Pass by value for small types, pointer for large
- Use `const` wherever data is read-only
- No mutable globals in `src/`

## Architecture (MUST RESPECT)

The layers are:
```
core → gfx → fonts/image/library/codec → ui → playback/settings/sound → hal → app → platform/sim & platform/device
```

Each module exposes only its `include/<module>/` directory. A module can only include headers from targets it links.

## Testing (MUST PASS)

Before any commit:
```bash
ctest --preset dev
```

This runs:
- Unit tests per module
- `test_app` (composed app through fake HAL)
- `sim_headless_smoke` (headless simulator)
- `no_comments` check (enforces no comments rule)
- `format` check (`.clang-format` compliance)
- `target_audit` (memory constraints for Cortex-M3)

## Build Presets

| Preset | Use |
|---|---|
| `dev` | Daily development, testing |
| `release` | Official firmware |
| `device` | Device build with memory validation |

## Generated Code Rules

NEVER edit by hand:
- `src/lang/packs/` ← `tools/lang_pack.py`
- `src/fonts/packs/` ← `tools/glyph_pack.py`
- `src/library/sort_fold_table.{c,h}` ← `tools/fold_table.py`

## Simulator (Your Main Development Tool)

```bash
# Setup system directory
mkdir -p build/system

# Run with music folder
./build/dev/platform/sim/echo_sim --card /path/to/music --system-dir build/system

# Headless testing (deterministic)
./build/dev/platform/sim/echo_sim --headless --frames 120 --keys down,down,play --screenshot test.bmp
```

## Memory Constraints (HARD LIMITS)

- No heap, no recursion, no VLAs
- Device stack: 16KB
- Code window: 157,292 bytes max (0x2666C)
- Total image: 437,600 bytes max
- Core 1 RAM: 384KB (decoders)
- Core 0 data RAM: ~288KB

## Development Flow

1. ✅ Check this file (CLAUDE.md) to understand project rules BEFORE starting
2. ✅ Run tests: `ctest --preset dev` 
3. ✅ Test in simulator
4. ✅ Only consider device build if tests pass and simulator works

If you deviate from these rules, the `no_comments` or `format` tests will fail.

## Common Commands

```bash
# Setup
python3 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt

# Build & Test
cmake --preset dev && cmake --build --preset dev && ctest --preset dev

# Run simulator
./build/dev/platform/sim/echo_sim --card ~/Music --system-dir build/system
```

---

**This file is read at the start of every session in this repository. Always check it before writing code.**