# EchoPod Quick Reference

## Development Rules Summary

### ✅ MUST DO
- Run `ctest --preset dev` - tests must pass
- Test in simulator before device
- Use correct naming conventions
- Keep functions under 20 lines
- Max 4 parameters per function
- Use `const` for read-only

### ❌ NEVER DO
- Write comments in C/H files
- Edit generated code in `src/lang/packs/`
- Edit generated code in `src/fonts/packs/`  
- Edit generated code in `src/library/sort_fold_table.*`
- Add dependencies across architecture layers
- Use heap, recursion, or VLAs
- Exceed memory limits (157KB code, 437KB total)

## Simulator Commands

```bash
# Basic
./build/dev/platform/sim/echo_sim --card ~/Music --system-dir build/system

# Headless/Deterministic testing
./build/dev/platform/sim/echo_sim --headless --frames 100 --keys play,down,back --screenshot test.bmp

# Custom battery
./build/dev/platform/sim/echo_sim --card ~/Music --battery 50 --charging
```

## Build Commands

```bash
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
```

## Architecture Layers

`hal` → `app` → `library,playback,settings,sound,ui` → `fonts,image,codec,dsp` → `gfx` → `core`

Never include headers from layers you don't depend on. Check `docs/architecture.md` for details.