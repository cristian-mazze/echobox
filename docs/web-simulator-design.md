# Web Simulator Design

## Purpose

A browser build of the existing simulator (`platform/sim`), compiled to WebAssembly with Emscripten.

It is **not** a replacement for the native simulator in the daily development loop. Rebuilding and relaunching `echo_sim` natively is faster than an `emcc` rebuild plus page reload, and native C debugging is far better than source maps in DevTools.

The web build exists for:

- **Demos**: a static page (for example GitHub Pages) where anyone can try EchoPod without installing anything.
- **Testers who do not build**: they can reproduce and report UI bugs with real screenshots.
- **Pull request previews**: one hosted build per branch for reviewers.

Fidelity is the main requirement. The page must run the same `app`, `ui`, `gfx`, `codec`, `dsp` and `playback` code as the device, pixel for pixel and sample for sample.

---

## 1. Starting Point

The simulator is already a complete HAL implementation over SDL2, about 2,500 lines in `platform/sim/`:

| HAL | Sim implementation | Dependencies | Web status |
|---|---|---|---|
| Display | `sim_display.c` | SDL renderer, RGB565 streaming texture | Works with the Emscripten SDL2 port |
| Input | `sim_input.c` | SDL key events, `sim_input_inject` | Works; on-screen buttons call `sim_input_inject` |
| Audio | `sim_audio.c` | `SDL_OpenAudioDevice`, `SDL_AudioStream` | Works; the AudioContext needs a user gesture to start |
| Clock | `sim_clock.c` | SDL timer | Works |
| Storage | `sim_storage.c` | `fopen`, `opendir`, `stat` | Works over the Emscripten virtual file system (see section 4) |
| Bluetooth | `sim_bluetooth.c` | none (mock) | Works as is |
| Power / system | `sim_power.c`, `sim_system.c` | none | Works as is |

The decoders are also plain C: `src/codec/` (FLAC, ALAC, MP4, OGG) and `third_party/` (dr_libs, faad2, libmad, opus). They compile with `emcc` unchanged. No JavaScript decoders, ffmpeg builds or browser-native codecs are used, because those would stop exercising the firmware's own decoding paths.

The simulator uses no threads, so the build does not need `SharedArrayBuffer`, `-pthread` or cross-origin isolation headers.

---

## 2. Required Changes

### 2.1 Main loop

`run()` in `platform/sim/main.c` is a blocking `while` loop paced with `SDL_Delay`, which freezes a browser tab. The frame body moves into its own function so both platforms share it:

```c
static bool sim_frame(SimSession *session, uint32_t *next_title_ms)
{
    if (!pump_events() || frame_limit_reached(session) || sim_power_off_requested()) {
        return false;
    }
    uint32_t frame_started_ms = hal_clock_milliseconds();
    inject_scripted_keys(session);
    app_step();
    session->frames++;
    finish_frame(session, frame_started_ms, next_title_ms);
    return true;
}
```

- Native: `run()` calls `sim_frame` in a loop, as today.
- Web: a separate `sim_web_main.c` registers a callback with `emscripten_set_main_loop_arg(callback, session, 0, true)`. The callback calls `sim_frame` and runs `finish()` and `emscripten_cancel_main_loop()` when it returns `false`. `wait_for_next_frame` is skipped there, because `requestAnimationFrame` provides the pacing.

The `#ifdef __EMSCRIPTEN__` checks stay out of shared code: the web entry point is its own file, selected in CMake.

### 2.2 Build

The build uses a CMake preset, not a hand-written `emcc` file glob. A glob skips modules (`codec`, `fonts`, `image`, `lang`, `dsp`, `library`) and bypasses the layering enforced by target links.

- `CMakePresets.json` gets a `web` configure preset: `binaryDir` `build/web`, the Emscripten toolchain file (`$env{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake`), `CMAKE_BUILD_TYPE=Release`, `ECHO_BUILD_TESTS=OFF`, `ECHO_BUILD_DEVICE=OFF`, `ECHO_SANITIZE=OFF`.
- `platform/sim/CMakeLists.txt`: when `EMSCRIPTEN` is set, replace `find_package(SDL2)` with `-sUSE_SDL=2` and build `echo_sim` from `sim_web_main.c` instead of `main.c`.
- Link options: `-sUSE_SDL=2 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB -lidbfs.js -sEXPORTED_RUNTIME_METHODS=FS,callMain --shell-file platform/web/shell.html -O2`. Use `-g -sASSERTIONS=1` for debug builds.
- Output: `echo_sim.html`, `echo_sim.js`, `echo_sim.wasm`, plus the font and language packs passed to `--preload-file`.

Fonts, language packs and menus are **not** trimmed for size. A few megabytes do not matter in a browser, and trimming would make the web build diverge from the device.

### 2.3 Page shell

`platform/web/shell.html` is a single static page with no bundler and no npm dependencies:

- The canvas with the 320×170 display, scaled with CSS `image-rendering: pixelated`.
- Buttons for Up, Down, Play, Menu, Back and Power, each calling an exported `sim_web_key(key, pressed)` that forwards to `sim_input_inject`. Long presses come from holding the button.
- A "Choose music folder" control (`<input type="file" webkitdirectory>`) plus drag and drop.
- A "Start" button that creates the session. It provides the user gesture required to unlock audio.
- A status line showing `document.title`. `SDL_SetWindowTitle` already writes the screen name and frame stats there.

---

## 3. Keyboard Mapping

The page reuses `KEY_BINDINGS` from `sim_input.c` unchanged:

| Key | Device key |
|---|---|
| Up | Volume down |
| Down | Volume up |
| Enter / Space | Play |
| M / Tab | Menu |
| Backspace / Escape | Back |
| P | Power |

The canvas must have focus, and the page calls `preventDefault` on these keys so Space and the arrow keys do not scroll the page.

---

## 4. Storage

This is the only part with a real design decision. `hal_file_read` is synchronous with an offset, while browser file APIs are asynchronous. `sim_storage.c` stays as it is; the choice is which Emscripten file system sits under `fopen` and `opendir`.

### Phase 1: MEMFS + IDBFS

- `/card` is MEMFS. When the user picks a folder, JavaScript writes each file into `/card/<relative path>` with `FS.writeFile`, then calls `callMain(["--card", "/card", "--system-dir", "/system"])`.
- `/system` is IDBFS, holding the library database and settings. It is loaded with `FS.syncfs(true)` before start and saved with `FS.syncfs(false)` after each `hal_file_write` / `hal_file_write_at`, through a hook in the web entry point, plus on `pagehide`.
- Limit: the whole card lives in WASM memory. That is fine for a sample library of a few albums (hundreds of MB at most).

### Phase 2: large libraries

Use WasmFS with the OPFS backend, or move the app into a Worker with WORKERFS and an `OffscreenCanvas`. Both give synchronous reads from user files without copying them into memory. This is only needed if testers use full libraries; it is not part of the MVP.

---

## 5. Audio

`sim_audio.c` already converts decoder output with `SDL_AudioStream` and feeds an SDL audio device. The Emscripten SDL2 port maps that device onto Web Audio, so no audio bridge is needed.

- Web Audio is only the PCM sink. Equalizer, filter, balance and volume curves stay in `src/dsp` and `src/sound`, so they are tested exactly as they ship.
- The AudioContext is created by the Start button, because of browser autoplay rules.
- Expect more output latency than native. It does not matter for UI and playback testing.

---

## 6. Testing

- `ctest --preset dev` is unchanged and remains the gate before commit.
- CI gets a job that configures and builds the `web` preset, so the web build cannot break silently.
- Optional later: a headless browser smoke test that loads the page, sends `--headless --frames 120 --keys down,down,play` through `callMain`, and compares the screenshot with the native `sim_headless_smoke` output. Both should match exactly, since they run the same code with virtual time.

---

## 7. Deployment

A static site: `echo_sim.html`, `echo_sim.js`, `echo_sim.wasm`, `echo_sim.data` (preloaded packs). Static hosting works because no special headers are required.

- **Release demo**: GitHub Pages, built from tags.
- **PR previews**: optional; a CI artifact or preview deployment per pull request.

Sample music is not committed. Testers bring their own folder, or the demo links to freely licensed albums they can download.

---

## 8. Out of Scope

These were considered and rejected:

- **JavaScript or browser-native codecs**: they would bypass the firmware decoders.
- **Rendering text with CSS, sprites, WebGL Cover Flow, Web Audio EQ**: they replace firmware code with web equivalents, so the result no longer matches the device.
- **Hot swapping the WASM module while keeping state**: not achievable in a reliable way. An incremental rebuild plus page reload is enough, and settings survive in IDBFS.
- **webpack, jest, puppeteer, nodemon, in-browser code editors, server-side compilation**: tooling with no payoff for a single static page.
- **Web Bluetooth**: the existing `sim_bluetooth.c` mock is enough.

---

## 9. Plan

| Step | Work | Estimate |
|---|---|---|
| 1 | Extract `sim_frame` from `run()`; native behavior and tests unchanged | 0.5 day |
| 2 | `web` preset, Emscripten branch in `platform/sim/CMakeLists.txt`, `sim_web_main.c` | 1 day |
| 3 | `shell.html`: canvas, buttons, Start, folder picker into MEMFS | 1 day |
| 4 | IDBFS for `/system` with sync after writes | 0.5 day |
| 5 | CI build of the `web` preset, GitHub Pages deploy | 0.5 day |
| **MVP** | | **about 3–4 days** |
| Later | Large libraries via OPFS or a Worker; browser screenshot parity test | as needed |

---

## 10. Setup

```bash
git clone https://github.com/emscripten-core/emsdk.git
./emsdk/emsdk install latest
./emsdk/emsdk activate latest
source ./emsdk/emsdk_env.sh
```

```bash
cmake --preset web
cmake --build --preset web
python3 -m http.server 8080 --directory build/web/platform/sim
```

Then open `http://localhost:8080/echo_sim.html`. Pages must be served over HTTP, not opened as `file://`.
