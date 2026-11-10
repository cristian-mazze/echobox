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
3. **Region plan: unchanged.** The rolling window is 9 cell rows × 112 × 3 channels × 4 B = 12,096 B. That covers chroma up to a vertical sampling factor of 2 when the cropped span is at least 224 px (see "Phase 1 result"), and factor 1 from 112 px. Narrower cases return `COVER_IMAGE_NEEDS_IDLE`.
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

### Phase 1 result

What was built, and where it differs from the steps above:

- **One grid type, two modes.** Instead of a `CoverTarget` table, `CoverGrid` now ends in a flexible array of `ring_rows` cell rows (112 for the full grid, 9 for the rolling one) and carries an optional `reach` hook. The decoders keep calling the same `cover_grid_*` functions, and the level 1 numbers do not change. `cover_grid_reach` does nothing when the hook is empty. This avoided a second indirection in the rescan overlay.
- **Row renderer in the main image.** The first build of the decoders passed the 86,016 B overlay limit by 292 B. `cover_render.c` (the cell to RGB565 conversion) now lives in its own library that `echo_device` links with `-Wl,-u`, and the overlay reuses it through `--just-symbols`. Moving all of `cover_grid.c` was tried first and it pushed code, `.data` and `.bss` to 438,740 B, over the 437,600 B limit.
- **Sizes after phase 1:** main code window 151,916 of 157,292 B, code plus `.data` and `.bss` 436,900 of 437,600 B, rescan overlay 85,460 of 86,016 B. `cover_grid_rolling.c` and `cover_image_rows.c` are not in the overlay map.
- **`fits` takes the band height.** `cover_grid_rolling_fits(width, height, band_height)` uses the pixel height of one decoder step (8 times the largest vertical sampling factor for JPEG, 1 for PNG). Worst case alignment gives `(span - 1 + band * 112 - 1) / span + 1` cell rows, which must be 9 or fewer. For a 16 px band (4:2:0) that needs a span of at least 224 px, not 222 as estimated in phase 0. A band of 8 px needs 112 px.
- **Extra result codes.** `COVER_IMAGE_NEEDS_IDLE` for progressive JPEG, baseline JPEG with one scan per component, Adam7 PNG, PNG rows over the row limit and spans the window cannot cover. `COVER_IMAGE_CANCELLED` when the sink returns false.
- **Proof.** `check_cover_decode.py` runs every good image in both modes and requires identical records, derives the expected `NEEDS_IDLE` cases from the image headers, and fuzzes both modes under the sanitizers. Pillow cannot write 4:1:1 JPEG, so 4:1:1 is covered only by the synthetic MCU-order test in `test_cover_grid_rolling`.

### Rolling decoder memory

Arena use of `cover_image_decode_rows` measured on the host with 64-bit pointers. The device is a few hundred bytes smaller.

| Part | JPEG | PNG |
|---|---|---|
| Rolling state (`held` row 1,344 B and bookkeeping) | 1,384 B | 1,384 B |
| Grid: totals 2,688 B, row scratch 1,344 B, window 12,096 B | 16,184 B | 16,184 B |
| Reader buffer | 4,096 B | 4,096 B |
| Decoder state, tables, blocks, inflate | 13,144 B | 37,400 to 39,992 B |
| Total | 34,808 B | 59,064 to 61,656 B |

The result is the same for every JPEG sampling and size tried, and it changes only with the PNG color type.

Changes this implies for phase 3:

- The window alone is 12,096 B, but the rolling grid needs 17,568 B: 5.4 KB more for the per channel totals, the row scratch and the held even row that pairs with the next odd row to make a half row.
- A JPEG decode needs 34.8 KB and a PNG decode about 61 KB (the inflate window dominates). Level 2 accepts only JPEG while playing, so the borrowed region plan can be sized for 35 KB.
- The totals are computed on the first `reach`. For a 2,000 px span that is one stretch of work with no yield point, which phase 2 must split or phase 3 must precompute.
- Rendering a row uses about 400 B of stack on top of the decoder.

### What phase 1 does not do

Phase 1 adds no yield hook (phase 2), no borrowed memory or overlay (phase 3), and no change to when covers fill (phase 4). The level 1 code keeps its current behavior.

## Phase 2: yield hook

Phase 2 lets `cover_image_decode_rows` give the caller a turn at least every `slice_ms` (2 ms by default), without a second stack. The caller is expected to run the audio service from that callback.

### What was built

- `CoverYield` (`image/cover_yield.h`): a clock, a `service` callback, `slice_ms`, and the longest stretch seen. `cover_yield_point` does nothing for a null hook, and otherwise calls `service` only after `slice_ms` have passed. A `service` that returns false cancels the decode for good.
- `cover_image_decode_rows` takes a `CoverRowsRequest` (source, memory, sink, optional yield). A refusal makes it return `COVER_IMAGE_CANCELLED`, whichever decoder noticed it first.
- Yield points: after every JPEG MCU (every block for single component scans), after every 4 KB card read, after every PNG row, after each rendered row of the rolling grid, and after each of the 2 to 6 steps that prepare the per channel totals. The totals are no longer one block at the first `reach`.
- The level 1 path passes no hook. The null check is inline, so it costs a compare per point.

### Proof

`check_cover_decode.py` runs every image with a fake clock that ticks once per reading (`--rolling --yield 3 0`) and requires the same bytes as the full grid. It also refuses service at the 20th yield (`--yield 1 20`) and requires `cancelled` for every image the rolling mode accepts. `test_cover_grid_rolling` repeats this on synthetic samples and also refuses during the totals, before any row is sent. `test_cover_yield` covers the hook alone.

### Longest stretch without a yield point

`cover_decode_tool --rolling --gaps IMAGE` reads a microsecond wall clock at every point. Best of nine runs on the host, with the sanitizers of the `dev` preset. The device is about 40 times slower, so the last column is only an estimate.

| Image | Yield points | Longest stretch | Device estimate |
|---|---|---|---|
| JPEG 4:2:0 600 x 600 | 1,571 | 95 us | 3.8 ms |
| JPEG 4:4:4 300 x 200 | 1,071 | 77 us | 3.1 ms |
| JPEG gray 500 x 400 | 2,996 | 64 us | 2.6 ms |
| JPEG 4:2:2 480 x 480, restart interval | 1,927 | 91 us | 3.6 ms |
| JPEG 4:4:4 160 x 160 (scale 1/1) | 521 | 114 us | 4.6 ms |
| JPEG 4:4:4 1000 x 1000 (scale 1/8) | 15,765 | 80 us | 3.2 ms |
| JPEG 4:4:4 1800 x 1200 | 8,626 | 80 us | 3.2 ms |
| PNG RGB 400 x 300 | 329 | 423 us | 17 ms |
| PNG RGB 500 x 500 | 510 | 399 us | 16 ms |

- Every baseline JPEG stays under the 10 ms ceiling and mostly under the 5 ms target. The stretch comes from one MCU, one rendered row, or the header, and it was not attributed further. The device bench in phase 4 has the final say.
- PNG stays above the ceiling because a row of inflate runs between two points, as phase 0 found. It remains paused only until inflate gets its own yield points.
- Sizes after phase 2: main code window 151,916 of 157,292 B, code plus `.data` and `.bss` 436,900 of 437,600 B, rescan overlay 85,724 of 86,016 B (292 B spare). The overlay grew by 264 B since phase 1 for the hook calls in the decoders and the reader.
- The yield callback runs on the decoder's stack. The 8,416 B peak of phase 0 has not changed, but the stack of the service it calls still has to be measured on top of that in phase 4.

## Phase 4, first part: start conditions and fallback

`app/cover_play_policy` holds the rules of the limits table as pure functions over a `CoverPlayInput` (playing, sample rate, `buffered_ms`, underruns), so they are tested without a device:

- It tracks the full `buffered_ms` level seen for the current track (`cover_play_observe`, reset by `cover_play_track_changed`) and freezes it when a session starts.
- `cover_play_may_start`: not disabled, playing, 1 to 48,000 Hz, and `buffered_ms` at 85 % of the full level.
- `cover_play_may_continue`: false below 50 % of the full level (the session is cancelled, level 2 stays on), and false for good after any new underrun.
- `cover_play_may_write`: card writes only at 85 % or more.
- `cover_play_note_gap`: a measured gap over 10 ms (for example `CoverYield.longest_ms` between two audio services) switches level 2 off until the next boot.

`test_cover_play_policy` covers each rule. Nothing calls the policy yet.

### The play fill path (library layer)

`cover_fill` takes an optional `CoverPlan.play` (`library/cover_play.h`). Without it nothing changes. With it, each cover is decoded by `cover_image_decode_rows` and written straight into a free record slot:

- `cover_record_writer` stages rows and writes them in chunks of 4,032 bytes (18 full rows or 36 half rows), full rows at the record start and half rows after them, byte for byte like `write_record`. Before each chunk it asks `may_write`, which the caller ties to `cover_play_may_write`; a refusal cancels the decode.
- The index entry (`cover_index_add` or `cover_index_replace`) is written only after the whole record is on the card, so an interrupted cover is never indexed. A cover that already exists is replaced only when a free slot is left; otherwise it is deferred and the old one stays.
- `CoverPlay.jpeg_only` (on by default in `cover_play_make`) turns PNG into `COVER_IMAGE_NEEDS_IDLE`. Anything the rolling decoder cannot take (progressive, non interleaved baseline, narrow crops) is deferred the same way: `CoverStats.deferred` counts it, the cursor stays on that album and `cover_fill` returns `SCAN_CANCELLED`, so the paused pass still finds it. Nothing is remembered as a failure.
- A refusal from the `CoverYield` service also returns `SCAN_CANCELLED`, with no record indexed and no failure noted.
- The writer lives in the decode memory, 8.1 KB on top of the 34.8 KB of a JPEG decode.
- The hook is a function pointer in `CoverPlay`, so `cover_play.c`, the writer and the rolling decoder are not linked into the rescan overlay (85,796 of 86,016 B). The overlay for level 2 has to link them.

`test_cover_play` checks, on the in-memory card: the covers written while playing are byte for byte the covers of the paused pass; every card write is 4 KB or less and each is gated; PNG is deferred with `jpeg_only` and the cursor stays; a refused service call and a denied write leave no cover and no failure, and a later pass finishes the library.

### Device measurement of the yield (music stopped)

Bench `spike/covers-l2-yield-bench` on the player at 200 MHz, each image decoded twice (slice 0 ms and 4 ms), rows written to the card in 4,032 B chunks.

- JPEG: longest gap between services 6.3 to 7.1 ms with slice 0 (9.6 ms for the 4:4:4 2000 px image), 8.0 to 10.1 ms with slice 4. The shortest indivisible stretch is about 6 to 7 ms, well above the host estimate of 2.6 to 4.6 ms. Decode time 0.9 to 13 s per image.
- PNG: longest gap 24 to 32 ms with any slice, growing with the image. Decode time 11 to 43 s.
- Progressive JPEG returns `COVER_IMAGE_NEEDS_IDLE` at once.
- Stack peak 4,256 B for JPEG and about 3.6 KB for PNG with the yield active, under the 8,416 B of phase 0.
- Card: 4 KB reads at most 3.3 ms, 4,032 B writes 5.5 to 6.5 ms on average and 9.0 ms at worst.

Decision: level 2 decodes baseline JPEG only (`jpeg_only` stays true, PNG and progressive wait for the paused pass) and the default slice is 2 ms. The margin against the 10 ms ceiling is thin and accepted: with a 4 ms slice the worst gap reached 10.1 ms, and the stretch below 6 ms cannot be reduced without more yield points inside the MCU work.

### What is still open

The rest needs the player, so it has not been started:

- **Where the decoder runs (measured, plan changed).** The target `echo_play_overlay` links `cover_fill` with the play path into `GLYPH_RAM` (0x03092000, 53,248 B) and reuses the symbols of the main image. The code alone is 41,196 B (decoders about 8 KB, tag readers about 5 KB, PNG still included), which leaves about 12 KB of that region for data. The JPEG decode needs 34.8 KB plus the 4 KB write chunk and the cover state, so code and data do not both fit in `GLYPH_RAM`, and `HIGH_RAM` has only 12,208 B free beside the 73,808 B of the main image. The data region would have to come from the part of core 1's RAM that the running codec leaves free (code area 0x01000000 to 0x01020000, minus the loaded program): FLAC leaves about 107 KB, AAC about 61 KB, MP3 about 53 KB and Opus none (its buffers fill the area). That needs the codec of the current track, and it puts core 0 traffic on the memory core 1 is decoding from, which has not been measured.
- **Wiring in the app (done, host only).** `CoverFiller` runs a playing session (`cover_filler_run_playing`) when Background covers is "Always", the player is playing, the screen is off, the battery allows it and `cover_play_policy` agrees. The audio service is the yield callback, a track change resets the guard, a deferred cover (PNG or progressive) or a missing play memory disables playing fills until boot, and the paused pass keeps working as before. The play fill takes its memory from `hal_system_borrow_play_memory` instead of core 1's RAM, and the scan entry carries `play_place` so the main image never links the decoder.
- **Device side (built, not yet run on the player).** The play overlay (`echo_play_overlay`, 41,196 B) is loaded from `/.echo/play_overlay.bin` into `GLYPH_RAM` by `hal_system_load_play_code` (`board/play.c`, magic and CRC checked, no audio shutdown). The glyph cache that lived there is rebuilt by `board_glyphs_start` when the session returns its memory, and nothing draws during a session. The data comes from the tail of core 1's code area, 4 KB past the loaded program and at least 48 KB (`hal_system_borrow_play_memory`): FLAC, AAC and MP3 qualify, Opus never does. A missing or bad overlay file, or too little memory, disables playing fills until boot and the paused pass is unaffected. To make room the image shrank by compiling more of `gfx` and `core` with `-Os`; it now sits exactly at the 437,600 B limit with the code at 152,592 B, and the rescan overlay at 85,804 B.
- **Device measurements.** The stack of the audio service on top of the decoder's 8.4 KB, the real gap between services, and the real overlay size.
