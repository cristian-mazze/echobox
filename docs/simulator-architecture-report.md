# EchoPod Simulator Architecture Report

## Executive Summary

The EchoPod simulator is a complete implementation of the Snowsky (FiiO) Echo Mini music player firmware, allowing it to run and test on a desktop environment using SDL2. It functions as a Hardware Abstraction Layer (HAL) implementation that bridges device code with desktop APIs.

---

## 1. High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           EXECUTION FLOW                                      │
└─────────────────────────────────────────────────────────────────────────────┘

┌──────────────┐     ┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│   main.c     │     │  sim_*.c     │     │   HAL API    │     │   App Code   │
│ (Entry)      │────▶│ (Implement.) │────▶│ (Interfaces) │────▶│ (Behavior)   │
└──────────────┘     └──────────────┘     └──────────────┘     └──────────────┘
       ▲                       ▲                    ▲                    │
       │                       │                    │                    │
       │  Arguments ──────────┼────────────────────┼────────────────────┤
       │  (--frames, --keys) │                    │                    │
       │                      │       SDL2 ────────┼────────────────────┤
       │                      │       (video/audio)│                    │
       │                      │                    │                    │
       │                      └────────────────────┼────────────────────┤
       │                                             │                    │
       └─────────────────────────────────────────────┘                    │
                                                                              │
                                                                              ▼
                                                                    ┌──────────────────┐
                                                                    │  hal_display_*.c │───▶ GPIO/Registers
                                                                    │  hal_clock.c     │───▶ Timer peripherals
                                                                    │  hal_input.c     │───▶ ADC keypad matrix
                                                                    └──────────────────┘
```

---

## 2. File Organization

### `platform/sim/` Directory Structure

```
platform/sim/
├── main.c                  # Main entry point
├── CMakeLists.txt          # Build configuration with SDL2 and targets
├── sim_options.c/h         # Command-line argument parsing
├── sim_script.c/h          # Key script for automated testing
├── sim_display.c           # Display HAL implementation (SDL_Renderer)
├── sim_input.c             # Input HAL implementation (SDL_Keyboard)
├── sim_clock.c             # Clock HAL implementation (SDL_GetTicks64)
├── sim_storage.c           # Storage HAL implementation (POSIX/stdio)
├── sim_audio.c             # Audio HAL implementation (SDL_Audio)
├── sim_power.c             # Power HAL implementation (simulated battery)
├── sim_system.c            # System services implementation
├── sim_glyphs.c            # Font loading
├── sim_decoder.c           # Audio decoders (dr_libs)
├── sim_codecs.c            # Codec wrappers
├── sim_bluetooth.c         # Bluetooth simulation (static)
├── sim_language.c          # Language pack loading
└── sim_power.c             # Battery and power-off simulation
```

---

## 3. Initialization Flow

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                        INITIALIZATION FLOW                                    │
└──────────────────────────────────────────────────────────────────────────────┘

main(int argc, char **argv)
    │
    ├─ 1. prepare_session()
    │     ├─> sim_options_parse() ──▶ Parses arguments:
    │     │     ├── --scale N        (window scale)
    │     │     ├── --frames N       (frame limit for headless)
    │     │     ├── --headless       (no video/audio)
    │     │     ├── --screenshot     (save final frame)
    │     │     ├── --keys script    (key sequence)
    │     │     ├── --card PATH      (music directory)
    │     │     ├── --system-dir     (system directory)
    │     │     ├── --battery N      (battery percent)
    │     │     └── --charging       (charging state)
    │     │
    │     └─> sim_script_parse()    (if --keys provided)
    │           └─> Programmes events in circular buffer
    │
    ├─ 2. SDL_SetMainReady()
    │
    ├─ 3. start_sdl()
    │     ├─> SDL_Init()             (subsystems based on headless)
    │     │     ├── headless? SKIP_VIDEO, SKIP_AUDIO
    │     │     └── normal?   VIDEO | TIMER | AUDIO
    │     │
    │     └─> sim_display_open()     (if not headless)
    │           ├─> SDL_CreateWindow()     (320x170 scaled)
    │           ├─> SDL_CreateRenderer()   (ACCELERATED)
    │           └─> SDL_CreateTexture()    (RGB565 streaming)
    │
    ├─ 4. System Configuration
    │     ├─> sim_storage_configure(card, system)
    │     ├─> sim_audio_start(headless)
    │     │     ├─> Audio device = SDL_OpenAudioDevice()
    │     │     └─> Stream = SDL_NewAudioStream() (resample rate)
    │     │
    │     ├─> sim_power_configure(battery_percent, charging)
    │     │     └─> Calculates millivolts = 3450 + (percent × 75)
    │     │
    │     ├─> sim_glyphs_attach()
    │     │     └─> Loads font packs from src/fonts/packs/
    │     │
    │     └─> app_start()
    │           └─> Initializes application layers:
    │                 ├── core (deadlines, strings, random)
    │                 ├── library_store (loads library.db)
    │                 ├── settings_store (loads settings.bin)
    │                 └── gfx (canvas, strip renderer)
    │
    ├─ 5. app_show_splash()
    │     └─> Shows "EchoPod" splash screen
    │
    └─ 6. run(&session) ◀─── MAIN LOOP ───────────────────────────────────────┐
              │                                                               │
              ▼                                                               │
        ┌──────────────────────────────────┐                                      │
        │  WHILE:                          │  ◄── SEE NOTES FOOTER             │
        │  ┌────────────────────────────┐  │                                      │
        │  │ pump_events()              │  │  ┌────────────────────────────────┐│
        │  │  ├─> SDL_PollEvent()       │  │  │ NOTE: Frame timing:            ││
        │  │  ├─> sim_input_handle_event│  │  │  • FRAME_PERIOD_MS = 16ms       ││
        │  │  │  (keys up/down)         │  │  │  • Virtual time in headless     ││
        │  │  └─> sim_input_inject()   │  │  │  • sim_clock_advance(16ms)      ││
        │  │     (to event queue)       │  │  │  • Audio tick each frame        ││
        │  │                            │  │  │                                 ││
        │  │  ┌──────────────────────────────┐│  │  └────────────────────────────────┘│
        │  │  │ inject_scripted_keys()       ││  │                                      │
        │  │  │  ┌─> Compare current frame  ││  │
        │  │  │  │  with programmed events  ││  │
        │  │  │  └─> sim_input_inject()     ││  │
        │  │  └────────────────────────────┘│  │
        │  │                                  │  │
        │  │  ┌────────────────────────────┐  │  │
        │  │  │ app_step()                 │  │  │
        │  │  │  ──► Calls full app loop    │  │  │
        │  │  └────────────────────────────┘  │  │
        │  │                                  │  │
        │  │  ┌────────────────────────────┐  │  │
        │  │  │ session->frames++            │  │  │
        │  │  └────────────────────────────┘  │  │
        │  │                                  │  │
        │  │  ┌────────────────────────────┐  │  │
        │  │  │ finish_frame()               │  │  │
        │  │  │  ├──> sim_clock_advance()   │  │  │
        │  │  │  ├──> sim_audio_tick()      │  │  │
        │  │  │  ├──> SDL_UpdateTexture()   │  │  │
        │  │  │  ├──> SDL_RenderCopy()      │  │  │
        │  │  │  └──> SDL_RenderPresent()   │  │  │
        │  │  └────────────────────────────┘  │  │
       ◄──┴──┴──┴────────────────────────────┴──┴──┘
```

---

## 4. HAL Component Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          HAL INTERFACE (hal/*.h)                              │
└─────────────────────────────────────────────────────────────────────────────┘

┌──────────────┐     HAL API Contract     ┌─────────────────┐
│              │ ◄───────────────────────▶│                 │
│   APP CODE   │                          │   DEVICE HAL    │
│              │                          │ (platform/dev.) │
└──────────────┘                          └─────────────────┘
                                                          │
                                                          │ implements
                                                          ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                         SIMULATOR HAL (platform/sim/)                       │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐               │
│  │   hal/display.h │  │   hal/input.h   │  │   hal/clock.h   │               │
│  │  ◀─────────────▶│  │  ◀─────────────▶│  │  ◀─────────────▶│               │
│  │ sim_display.c   │  │ sim_input.c     │  │ sim_clock.c     │               │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘               │
│                                                                             │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐               │
│  │   hal/audio.h   │  │   hal/storage.h │  │   hal/power.h   │               │
│  │  ◀─────────────▶│  │  ◀─────────────▶│  │  ◀─────────────▶│               │
│  │ sim_audio.c     │  │ sim_storage.c   │  │ sim_power.c     │               │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘               │
│                                                                             │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐               │
│  │  hal/system.h   │  │ hal/bluetooth.h │  │ hal/languages.h │               │
│  │  ◀─────────────▶│  │  ◀─────────────▶│  │  ◀─────────────▶│               │
│  │ sim_system.c    │  │ sim_bluetooth.c │  │ sim_language.c  │               │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘               │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│                    DEPENDENCY MAP (CMake)                                     │
└─────────────────────────────────────────────────────────────────────────────┘

echo_sim (executable)
    │
    ├─▶ echo_app                     (main logic)
    │      │
    │      ├─▶ echo_core              (deadlines, strings, random)
    │      ├─▶ echo_gfx               (rendering)
    │      ├─▶ echo_ui                (screens)
    │      ├─▶ echo_library           (music database)
    │      ├─▶ echo_playback          (player)
    │      ├─▶ echo_settings          (settings)
    │      ├─▶ echo_sound             (DSP)
    │      └─▶ echo_hal               (hardware interfaces)
    │
    ├─▶ echo_sim_glyphs              (fonts → SDL_Texture)
    │      └─▶ echo_gfx
    │
    ├─▶ echo_sim_language            (languages → SDL_RWops)
    │      └─▶ echo_hal
    │
    ├─▶ echo_library_scan            (file indexing)
    │
    ├─▶ echo_sim_decoder             (decoders → PCM buffer)
    │      ├──▶ echo_sim_codecs       (dr_libs wrappers)
    │      ├──▶ SDL2::SDL2
    │      └──▶ echo_hal
    │
    ├─▶ echo_sim_options             (args → SDL_Log)
    │
    ├─▶ echo_sim_storage             (filesystem → stdio.h)
    │
    ├─▶ echo_sim_audio               (audio → SDL_AudioStream)
    │      │
    │      ├──▶ echo_hal
    │      ├──▶ echo_sim_decoder
    │      └──▶ SDL2::SDL2
    │
    ├─▶ SDL2::SDL2
    │
    └─▶ echo_codec                   (FLAC, ALAC, etc.)
```

---

## 5. Module Technical Details

### 5.1 Display (`sim_display.c`)

| Feature | Value |
|---------|-------|
| Resolution | 320 × 170 pixels |
| Format | RGB565 (16-bit) |
| Buffering | Static panel in memory (`uint16_t panel[320][320]`) |
| Rendering | Strip renderer (320×34 pixels per frame) |
| Scale | 1-8× (configurable) |
| SDL Objects | Window, Renderer, Texture |

**Rendering Flow:**
```
hal_display_write(x,y,w,h,pixels)
    └─▶ Copy to display.panel[y+row][x]
        │
        └─▶ hal_display_present()
                └─▶ SDL_UpdateTexture() → show_panel() → SDL_RenderCopy() → SDL_RenderPresent()
```

### 5.2 Audio (`sim_audio.c`)

| Component | Detail |
|-----------|--------|
| Target rate | 48 kHz |
| Format | 32-bit float (internal) → STEREO → 48 kHz output |
| Color buffers | 2 slots (current/next) |
| Decoders | FLAC, MP3, WAV, MP4/M4A, Opus |
| Libraries | dr_libs (drflac, drmp3, drwav, dr_mp4) |

**Audio Architecture:**
```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│  sim_decoder │────▶│  sim_audio   │────▶│  SDL_Audio   │
│              │     │              │     │              │
│ PCM format   │     │ Equalizer  │     │ 48kHz stream │
│ Normalized   │     │ Gain       │     │              │
└──────────────┘     └──────────────┘     └──────────────┘
```

### 5.3 Storage (`sim_storage.c`)

| Method | Implementation |
|--------|----------------|
| Open file | `fopen()` with slot management |
| Read file | `fread()` |
| Write file | Writes to `.tmp` → `rename()` (atomic) |
| Directory | `opendir()` / `readdir()` |
| Reserve | `ftruncate()` creates file at specified size |

**Paths:**
```
/path/to/music           → Card directory (music)
/path/to/system/.echo    → System directory (library.db, settings.bin, etc.)
```

### 5.4 Clock (`sim_clock.c`)

```c
// Virtual mode (headless/testing)
bool virtual_time;
uint32_t virtual_ms;

uint32_t hal_clock_milliseconds(void) {
    return virtual_time ? virtual_ms : SDL_GetTicks64();
}

// Manual advance
void sim_clock_advance(uint32_t ms) {
    virtual_ms += ms;
}
```

---

## 6. Command-Line Options

| Option | Description | Example |
|--------|-------------|---------|
| `--scale N` | Window scale (1-8) | `--scale 3` |
| `--frames N` | Frame limit (headless) | `--frames 120` |
| `--headless` | No video/audio | `--headless --frames 100` |
| `--screenshot FILE` | Save final frame | `--screenshot test.bmp` |
| `--keys "down,down,play"` | Key script | `--keys play:long` |
| `--card PATH` | Music directory | `--card ~/Music` |
| `--system-dir PATH` | System directory | `--system-dir ./build/system` |
| `--battery N` | Battery percentage | `--battery 80` |
| `--charging` | Charging state | `--charging` |

---

## 7. Audio Playback Pipeline

```
┌──────────────────────────────────────────────────────────────────────────┐
│                        AUDIO PLAYBACK PIPELINE                             │
└──────────────────────────────────────────────────────────────────────────┘

hal_audio_play(path, start_ms, token)
    │
    ├─▶ open_track() ◀──▶ sim_decoder_open()
    │     │              ├─▶ Detect extension (.flac/.mp3/.wav/.m4a/.opus)
    │     │              ├─▶ call open_flac() / open_mp3() / etc.
    │     │              └─▶ Uses dr_libs to open native file
    │     │
    │     └─▶ Uses SDL_AudioStream for resampling if needed
    │
    ├─▶ State changes to HAL_AUDIO_PLAYING
    │
    │   (in SDL callback thread)
    │       ▼
    │    fill_audio() ──▶ decode_chunk()
    │                       ├─▶ sim_decoder_read_stereo()
    │                       │       ├─▶ drflac_read_pcm_frames_f32()
    │                       │       ├─▶ drmp3_read_pcm_frames_f32()
    │                       │       └─▶ etc.
    │                       │
    │                       ├─▶ equalize_chunk() (if EQ active)
    │                       │
    │                       └─▶ SDL_AudioStreamPut() → SDL audio device
    │
    └─▶ sim_audio_tick(elapsed_ms)
            ▶ Advances decoder frame virtually (headless)
```

---

## 8. Headless Testing (Smoke Test)

```bash
# Run deterministic test
./build/dev/platform/sim/echo_sim \
    --headless \
    --frames 120 \
    --keys down,down,play \
    --screenshot test.bmp

# Verify output
ls -la test.bmp  # Should exist
```

**CI Configuration:**
```cmake
# CTest preset dev
test:
  - unit tests (per module)
  - test_app (app through fake HAL)
  - sim_headless_smoke
  - no_comments (no comments rule)
  - format (.clang-format)
  - target_audit (Cortex-M3 memory limits)
```

---

## 9. Memory and Resource Limits

| Resource | Limit | Simulator Usage |
|----------|-------|-----------------|
| Device stack | 16 KB | N/A |
| Code window | 157,292 bytes (0x2666C) | ~60% |
| Total image | 437,600 bytes | ~50% |
| Core 1 RAM | 384 KB | ~80% (decoders) |
| Core 0 RAM | ~288 KB | ~70% |
| Open files | 8 max | Used by storage |
| Open directories | 16 max | Used by library scan |

---

## 10. Frame Cycle Sequence Diagram

```
┌───────┐     ┌───────────┐     ┌──────┐     ┌──────┐
│ SDL   │     │   app     │     │ gfx  │     │ HAL  │
│ Event │     │  step()   │     │ draw │     │disp  │
└───────┘     └───────────┘     └──────┘     └──────┘
    │             │               │          │
    │ poll_event  │               │          │
    ▼             ▼               ▼          ▼
┌─────────────────────────────────────────────────┐
│ SIMULATOR: FRAME CYCLE                          │
├─────────────────────────────────────────────────┤
│                                                 │
│  1. pump_events()                               │
│     └─▶ SDL_PollEvent() → sim_input_*.c         │
│                                                 │
│  2. app_step()                                  │
│     ├─▶ input_processing()                     │
│     │     └─▶ hal_input_next_transition()       │
│     │                                           │
│     ├─▶ player_update()                        │
│     │     └─▶ hal_audio_*()                     │
│     │                                           │
│     ├─▶ screen_draw()                            │
│     │     └─▶ gfx_*() → canvas → strip buffer   │
│     │                                           │
│     └─▶ frame_stats_capture()                    │
│                                                 │
│  3. hal_display_present()                       │
│     ├─▶ SDL_UpdateTexture()                      │
│     ├─▶ SDL_RenderCopy()                         │
│     └─▶ SDL_RenderPresent()                      │
│                                                 │
│  4. sim_audio_tick()                            │
│     └─▶ Advances virtual timestamps             │
│                                                 │
└─────────────────────────────────────────────────┘
```

---

## 11. SDL2 Technical Specifications

```
SDL2 Video:
├── Window: 320×170 base, configurable scale (1-8×)
├── Renderer: SDL_RENDERER_ACCELERATED
├── Texture: SDL_PIXELFORMAT_RGB565, STREAMING
└── Output: 60 FPS target (16.67ms per frame)

SDL2 Audio:
├── Device: 48 kHz, 32-bit float, stereo
├── Callback: fill_audio() (pull-based)
├── Stream: SDL_NewAudioStream() resampler
└── Buffer: 1024 frames colors

SDL2 Input:
├── Keyboard mappings:
│   UP → KEY_VOLUME_DOWN
│   DOWN → KEY_VOLUME_UP  
│   SPACE/RETURN → KEY_PLAY
│   M/TAB → KEY_MENU
│   BACKSPACE/ESCAPE → KEY_BACK
│   P → KEY_POWER
└── Script parser: "play", "play:long", etc.
```

---

## 12. Output Files

| File | Purpose |
|------|---------|
| `.bmp` | Screenshot of final frame (SDL_SaveBMP) |
| `.db` | Library database |
| `.bin` | Settings, resume, favorites, covers |
| stdout log | Frame statistics (avg/worst time) |

---

## Appendix A: Build Commands

```bash
# Setup environment
python3 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt

# Build & Test
cmake --preset dev && cmake --build --preset dev && ctest --preset dev

# Run simulator
./build/dev/platform/sim/echo_sim --card ~/Music --system-dir build/system

# Headless testing (deterministic)
./build/dev/platform/sim/echo_sim --headless --frames 120 --keys down,down,play --screenshot test.bmp
```

---

## Appendix B: Architecture Layering

```
Layers (from bottom to top):

  core → gfx → fonts/image/library/codec → ui → 
  playback/settings/sound → hal → app → 
  platform/sim & platform/device
```

Each module exposes only its `include/<module>/` directory. A module can only include headers from targets it links.

---

## Appendix C: Complete Architecture Diagram

```
                      ┌─────────────────────────────────────────────────────────────────┐
                      │              EchoPod Simulator - Complete Architecture            │
                      ├─────────────────────────────────────────────────────────────────┤
                      │                                                                 │
      ┌───────────────┘       platform/sim/          └───────────────────┐           │
      │                   ┌───────────────────┐                        │           │
      │   ┌──────────────┼────── main.c ──────┼────────────────────────┼───────────┤
      │   │              └────────┬───────────┘                        │           │
      │   │                       │                                      │           │
      │   │              ┌────────┴───────────┐                          │           │
      │   │              │    SimSession      │                          │           │
      │   │              │    (options,      │                          │           │
      │   │              │     events,       │                          │           │
      │   │              │      frames)      │                          │           │
      │   │              └────────┬───────────┘                          │           │
      │   │                       │                                      │           │
      │   └───────────────────────┼──────────────────────────────────────┘           │
      │                           │                                                  │
      │              ┌────────────┴────────────┐                                       │
      │              │          HAL              │  (Interface Definitions only)       │
      │              ├───────────────────────────┼───────────────────────────────────────┤
      │              │  hal_display_*            │                                       │
      │              │  hal_input_*              │                                       │
      │              │  hal_audio_*              │                                       │
      │              │  hal_clock_*              │                                       │
      │              │  hal_storage_*            │                                       │
      │              │  hal_power_*              │                                       │
      │              │  hal_system_*             │                                       │
      │              │  hal_bluetooth_*          │                                       │
      │              │  hal_language_*           │                                       │
      │              └────────────┬────────────┘                                       │
      │                           │                                                  │
      │         ┌─────────────────┼─────────────────┐                                  │
      │         │                 │                 │                                  │
      │         ▼                 ▼                 ▼                                  │
      │  ┌──────────┐    ┌──────────────┐  ┌─────────────┐                          │
      │  │   ui     │    │   playback   │  │    gfx      │                          │
      │  │          │    │              │  │             │                          │
      │  │ Screen   │    │ Player       │  │ Strip       │                          │
      │  │ Stack    │    │ Queue        │  │ Renderer    │                          │
      │  │ Slide    │    │ Resume       │  │ Frame Diff  │                          │
      │  │ Header   │    │              │  │ Canvas      │                          │
      │  └──────────┘    └──────────────┘  └─────────────┘                          │
      │         │                 │                 │                                  │
      │         └─────────────────┼─────────────────┘                                  │
      │                           │                                                  │
      │                           ▼                                                  │
      └───────────────────────────┼───────────────────────────────────────────────────┘
                                  │
                      ┌─────────────┴─────────────┐
                      │              core           │
                      │   (deadline, key_tracker, │
                      │    frame_stats, etc.)      │
                      └───────────────────────────┘
```

---

## Appendix D: Device vs Simulator HAL Comparison

| Aspect | Device HAL | Simulator HAL |
|--------|------------|---------------|
| **Display** | Panel DMA write, 320×170 RGB565 | SDL2 texture update, same dimensions |
| **Input** | ADC key ladder, 6 keys | SDL keyboard events |
| **Clock** | SysTick 24-bit downcounter | SDL_GetTicks64() or virtual |
| **Audio** | Core 1 FLAC decoder + DMA | dr_libs + SDL audio stream |
| **Storage** | MMC/eMMC FAT + SD card | POSIX files / opendir |
| **Power** | Battery gauge + PMIC | Simulated mV calculation |
| **Boot** | Flash boot ROM | Instant from ELF |

---

## Appendix E: Key Code Paths

### Input Event Flow
```
User presses "PLAY" key
    ↓
SDL_KEYDOWN event in main.c
    ↓
sim_input_handle_event()
    ↓
hal_input_next_transition() ← Queue
    ↓
key_tracker_update() ← core/key_tracker
    ↓
navigation_intent() ← ui/navigation
    ↓
screen.handle_key() ← Specific screen
    ↓
app_step() continues
```

### App Start Sequence
```
app_start()
    ↓
    ├─> apply_language(ENGLISH)
    ├─> key_tracker_init()
    ├─> card_files_prepare() → Reserve .bin files
    ├─> settings_store_load() → Load settings.bin
    ├─> library_store_load() → Open library.db
    ├─> equalizer_store_load() → Load EQ settings
    ├─> start_screens() → Initialize screen stack
    ├─> bluetooth_link_start()
    └─> report_card_files() → Check for rescan
```

---

## Appendix F: Technical Reference

### Frame Timing Constants
```c
FRAME_PERIOD_MS = 16     // 60 FPS target
IDLE_RENDER_MS = 100     // Minimum frame interval
STRIP_HEIGHT = 34        // Vertical strip size
PANEL_WIDTH = 320        // Display width
PANEL_HEIGHT = 170      // Display height
```

### Key Gesture Types
```c
KEY_GESTURE_PRESS    // Simple tap
KEY_GESTURE_LONG     // Hold > 500ms
KEY_GESTURE_REPEAT   // Repeats while held
KEY_GESTURE_THRUST   // Tap on release + hold + repeat
```