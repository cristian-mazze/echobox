# Level 2 covers: decoding while music plays with the screen off

Level 1 fills covers while playback is paused and the screen is off (see "Background covers" in `docs/architecture.md`). Level 2 is meant to do the same work while music plays. The rule is that playback wins: no underruns, gapless intact, no latency anyone can notice. If in doubt, level 2 switches itself off.

This document holds the phase 0 measurements, the limits taken from them, and the step-by-step plan for phase 1.

## Phase 0: measurements

### Art survey

`tools/art_survey.py` was run on the reference library: 872 albums, 868 with art.

- Every piece of art is an embedded baseline JPEG: 859 are 4:4:4, 8 are grayscale and 1 is 4:2:0.
- No baseline JPEG uses one scan per component.
- No art is progressive JPEG or PNG.
- Art is mostly 640 × 640, with sides from 500 to 2000 px.

That makes 100 % of the albums eligible for level 2. A Pillow cross-check agreed on 59 of 59 samples.

### Device bench

The bench firmware (`spike/covers-l2`, 35130d5) ran on the reference player at 200 MHz with the screen on. Each of the six codec tracks was played, then the bench either starved the audio service on purpose or interleaved card I/O with it. With music stopped, it decoded the 12 images of `bench_art/`.

#### Audio tolerance

| Track | Full buffer | 5 and 10 ms gaps | 20 ms gaps | 40 ms gaps | Longest service call |
|---|---|---|---|---|---|
| FLAC 16/44.1 | 317 ms | 0 underruns | 0 (buffer half gone) | underruns | 3.8 ms |
| MP3 320 | 317 ms | 0 underruns | 0 underruns | underruns | 2.2 ms |
| AAC 256 | 317 ms | 0 underruns | 0 underruns | underruns | 4.0 ms |
| Opus 160 | 290 ms | 0 underruns | 0 underruns | underruns | 6.9 ms |
| FLAC 24/96 | 141 ms | 0 underruns | underruns | underruns | 5.6 ms |
| FLAC 24/192 | 56 ms | 0 underruns | underruns | underruns | 13.9 ms |

A gap is the time the audio service is left alone, repeated for 3 s. The ring refills only a little per service call, so the sustained gap matters more than the size of the ring.

#### Card I/O

| Operation | Average | Max |
|---|---|---|
| 4 KB read from `covers.bin` | 3.3 ms | 5.6 ms |
| 512 B write | 1.8 ms | 5.4 ms |
| 4 KB write | 2.7 ms | 6.8 ms |

When each read or write was interleaved with an audio service call, there were 0 underruns on every track, including FLAC 24/192.

#### Decoding, music stopped

| Image | Bytes | Total | CPU | Render |
|---|---|---|---|---|
| JPEG 4:4:4 600 × 600 | 208 KB | 1.87 s | 1.79 s | 78 ms |
| JPEG gray 640 × 640 | 216 KB | 1.27 s | 1.19 s | 26 ms |
| JPEG 4:4:4 1000 × 1000 | 548 KB | 2.66 s | 2.45 s | 78 ms |
| JPEG 4:4:4 1400 × 1400 | 2.95 MB | 13.95 s | 12.85 s | 78 ms |
| JPEG 4:4:4 2000 × 2000 | 2.76 MB | 13.79 s | 12.76 s | 79 ms |
| JPEG 4:2:0 2000 × 2000 (synthetic) | | 6.22 s | 5.70 s | 76 ms |
| JPEG progressive 2000 (synthetic) | | 4.47 s | 4.02 s | 76 ms |
| PNG RGB 1000 (synthetic) | | 12.34 s | 11.85 s | 75 ms |
| PNG RGB 2000 (synthetic) | | 46.80 s | 45.21 s | 80 ms |

- The cost is roughly 1 s plus 5 s per MB of JPEG. The device is about 40 times slower than the simulator, so simulator timings say nothing about the device.
- Peak stack use while decoding: 8,416 B for JPEG and 7,364 B for PNG, out of 16 KB.

#### Longest stretch without a yield point

| Yield point | Baseline JPEG | Progressive JPEG | PNG |
|---|---|---|---|
| Every MCU | 0.1 to 0.4 ms of work | 1.4 s block | no MCU points |
| Every JPEG row or PNG row | 20 to 51 ms | 1.4 s | 12 to 23 ms |
| Final render | 26 to 79 ms in one block | same | same |

The render converts the 112 × 112 sums to RGB565 with one 64-bit division per cell and channel, and it never yields.

#### Screen-off app steps

An app step with the screen off takes 0.3 to 0.7 ms on average. The worst recorded step, 10.5 s, is a level-1 fill session while paused, which is the expected behavior.

## Limits for level 2

| Limit | Value | Why |
|---|---|---|
| Work between two audio services | 5 ms target, 10 ms hard ceiling | 10 ms gaps never underran; 20 ms gaps underran at 96 and 192 kHz |
| Yield granularity | per MCU, with a clock check (yield once 4 ms have passed) | an MCU is 0.1 to 0.4 ms, a row is up to 51 ms |
| Render | one 112-px row (about 0.7 ms) per step | the full render is up to 79 ms |
| Card reads | one 4 KB read per step | max 5.6 ms |
| Card writes | chunks of 4 KB or less, one per step, only above the high mark | max 6.8 ms, no underruns when interleaved |
| Sample rates | 48 kHz or less to start with | 96 kHz has a 141 ms ring and no margin past 10 ms gaps; 192 kHz has 56 ms and 14 ms service calls |
| High `buffered_ms` mark | 85 % of the full level seen when the session starts | the full level varies from 141 to 317 ms between formats |
| Low `buffered_ms` mark (cancel) | 50 % of that full level | |
| Art accepted while playing | baseline interleaved JPEG | progressive JPEG and PNG have long stretches without yields |

## Design decisions after phase 0

1. **Rolling grid: confirmed.** It also removes the 79 ms render block, because each 112-px row is converted as soon as it is complete. Progressive JPEG and PNG stay "paused only" in the first release, which costs nothing on the reference library. PNG still gets the rolling mode in phase 1, so that it can join later once inflate has yield points.
2. **Cover overlay in `GLYPH_RAM`: confirmed, by estimate.** The decoder, the lookup and the writer come to about 22 KB without tag readers, against 53,248 B. Phase 3 links the real overlay and checks it.
3. **Region plan: unchanged.** The rolling window is 9 cell rows × 112 × 3 channels × 4 B = 12,096 B. That covers chroma up to a vertical sampling factor of 2 when the cropped span is at least 222 px, and factor 1 from 112 px. Narrower cases return `COVER_IMAGE_NEEDS_IDLE`.
4. **Cooperative yield without a separate stack: confirmed.** The decoder peaks at 8.4 KB of stack. The background step it calls must be measured on top of that in phase 4, against 16 KB.
5. **Tidal writes: confirmed.** Writes are fast enough to go straight to the card, so there is no fallback to keep records in RAM.
6. **Start conditions: changed.** The sample rate limit drops from 96 kHz to 48 kHz. The other conditions stay as they were.
7. **Automatic fallback: unchanged.** An underrun, or a measured gap over 10 ms, switches level 2 off until the next boot.

Two phase 0 items are still open. Phase 3 links the real overlay to replace the size estimate, and phase 4 measures the stack and the cost of the background step together.

## Phase 1 plan: rolling grid and row sink

Phase 1 adds the rolling output mode to `src/image` and proves that it matches the full grid bit for bit. The level 1 decoder must not change behavior or grow past its overlay. Every step is a green commit: `ctest --preset dev`, the device build, and the sizes from the `.map` files.

The starting sizes are a main code window of 151,068 of 157,292 B and a rescan overlay of 85,628 of 86,016 B. The level 1 decoder (`jpeg.c`, `png.c`, `inflate.c`, `cover_grid.c`) is linked into the rescan overlay, which has only 388 B to spare, so every step checks the overlay size.

### 1.1 Render one row at a time

- Split `cover_grid_render` into `cover_grid_render_row` (one 112-px row of RGB565) and `cover_grid_render_half_row` (one 56-px row from two cell rows). `cover_grid_render` becomes a loop over them.
- The conversion of one cell row works on plain pointers: the sums and weights of each channel for that row. The rolling grid can then reuse it on its ring buffer and match by construction.
- Tests: `test_cover_grid` gets a case that compares the row by row output with the whole render, and `cover_decode_matches_pillow` stays green.
- Commit: `refactor: Cover grids render one row at a time`

### 1.2 Rolling grid

- New `src/image/include/image/cover_row_sink.h` with `CoverRowSink { bool (*full_row)(void *context, uint32_t y, const uint16_t *pixels); bool (*half_row)(void *context, uint32_t y, const uint16_t *pixels); void *context; }`. Returning false cancels the decode.
- New `cover_grid_rolling.{c,h}`:
  - `CoverGridRolling` keeps a ring of `COVER_ROLLING_ROWS = 9` cell rows per channel, with the same wrapping `uint32_t` sums as `cover_grid`.
  - It has the same calls for adding samples (`add`, `add_color`, `row_start`, `row_pixel`, `row_finish`, `touches`, `set_sampling`).
  - `cover_grid_rolling_reach(grid, y)` tells it that every decoded pixel above `y` has arrived. Each cell row that can no longer receive samples is then rendered, sent to the sink, and cleared for reuse.
  - Each pair of finished rows also produces one 56-px half row.
- `cover_grid_rolling_fits(width, height, sampling)` answers whether 9 rows are enough for this span and sampling. It holds the window arithmetic from phase 0, with a `_Static_assert` on the struct size.
- Tests in `tests/image/test_cover_grid_rolling.c`: synthetic samples fed in JPEG MCU order (4:4:4, 4:2:2, 4:2:0, 4:1:1) and in PNG row order, over several spans and off-centre crops. The rows from the sink must match `cover_grid_render` on a full grid fed with the same samples, bit for bit, and so must the cancellation.
- Commit: `feat: A rolling cover grid hands out finished rows`

### 1.3 One decoder, two kinds of grid

- `jpeg_decode` and `png_decode` stop calling `cover_grid_*` directly. They go through a small `CoverTarget` table: `add`, `add_color`, `touches`, `set_sampling`, `row_start`, `row_pixel`, `row_finish` and `reach`.
- `cover_image_decode` (level 1) passes the full grid table, so the decode and the render stay the same.
- **Size gate:** rebuild the device image and read `echo_rescan_overlay.map`. If the overlay passes 86,016 B, move `cover_grid.c` and the table into the main image, which has about 6 KB free, the way the overlay already reuses main-image symbols through `--just-symbols`. If that is still not enough, stop and report before changing anything else.
- Tests: `cover_decode_matches_pillow`, `cover_build_matches_reference` and the fuzz rounds all stay green, and the level 1 output does not change.
- Commit: `refactor: Image decoders write through a cover target`

### 1.4 Rolling entry point and `COVER_IMAGE_NEEDS_IDLE`

- `cover_image_decode_rows(ImageSource source, const CoverImageMemory *memory, CoverRowSink sink)` decodes baseline JPEG and non-interlaced PNG into the rolling grid.
- It returns `COVER_IMAGE_NEEDS_IDLE` for progressive JPEG, Adam7 PNG, PNG rows over the row limit, and any span or sampling the window cannot cover.
- `jpeg.c` calls `reach` after each MCU row and `png.c` after each image row.
- `CoverImageMemory` is a fixed memory plan: decoder state, tables, MCU blocks, reader and rolling window. Its size is checked with `sizeof` and `_Static_assert`, so phase 3 can map it onto the borrowed regions.
- The level 1 path does not use it, so the device linker drops it from the rescan overlay. The map shows whether that happened.
- Commit: `feat: Baseline JPEG and plain PNG decode into the rolling grid`

### 1.5 Bit-exact proof on every image

- `cover_decode_tool` gets a `--rolling` mode that writes the same record from the row sink.
- `check_cover_decode.py` runs every good case in both modes and requires identical bytes. It expects `NEEDS_IDLE` for progressive and Adam7, and runs the fuzz rounds in rolling mode under the sanitizers of the `dev` preset.
- New generated cases:
  - 4:4:4, 4:2:2, 4:2:0 and 4:1:1;
  - with and without restart intervals;
  - sizes that pick each DCT scale (1/1, 1/2, 1/4 and 1/8);
  - non-square images whose crop is off centre.
- Commit: `test: Rolling decodes match the full grid on every image`

### 1.6 Memory report

- Measure the peak memory of the rolling mode for each kind of image (state, tables, blocks, reader and window) and add it to this document. Note any change it implies for the region plan of phase 3.
- Commit: `docs: Rolling decoder memory`

### What phase 1 does not do

Phase 1 adds no yield hook (phase 2), no borrowed memory or overlay (phase 3), and no change to when covers fill (phase 4). The level 1 code keeps its current behavior.
