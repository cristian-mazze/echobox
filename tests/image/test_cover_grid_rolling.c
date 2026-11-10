#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "image/cover_grid.h"
#include "image/cover_grid_rolling.h"
#include "image/cover_yield.h"

enum {
    FULL = COVER_GRID_SIZE,
    HALF = COVER_GRID_HALF,
    BLOCK = 8
};

typedef struct Collected {
    uint16_t full[COVER_GRID_CELLS];
    uint16_t half[COVER_GRID_HALF_CELLS];
    uint32_t full_rows;
    uint32_t half_rows;
    uint32_t stop_after;
} Collected;

typedef struct Shape {
    uint32_t width;
    uint32_t height;
    uint32_t h[3];
    uint32_t v[3];
} Shape;

typedef struct Tick {
    uint32_t now;
    uint32_t services;
    uint32_t cancel_at;
} Tick;

static Tick tick;
static CoverYield *test_yield;

static uint32_t tick_now(void *context)
{
    (void)context;
    return tick.now++;
}

static bool tick_service(void *context)
{
    (void)context;
    tick.services++;
    return tick.cancel_at == 0 || tick.services < tick.cancel_at;
}

static CoverYield start_yield(uint32_t cancel_at)
{
    tick = (Tick){0, 0, cancel_at};
    CoverYield yield = cover_yield_make(tick_now, tick_service, NULL);
    yield.slice_ms = 1;
    cover_yield_begin(&yield);
    return yield;
}

static bool take_full(void *context, uint32_t y, const uint16_t *pixels)
{
    Collected *collected = context;
    CHECK_EQUAL(collected->full_rows, y);
    memcpy(collected->full + y * FULL, pixels, FULL * sizeof pixels[0]);
    collected->full_rows++;
    return collected->full_rows < collected->stop_after;
}

static bool take_half(void *context, uint32_t y, const uint16_t *pixels)
{
    Collected *collected = context;
    CHECK_EQUAL(collected->half_rows, y);
    memcpy(collected->half + y * HALF, pixels, HALF * sizeof pixels[0]);
    collected->half_rows++;
    return true;
}

static int32_t value_at(uint32_t channel, uint32_t x, uint32_t y)
{
    uint32_t hash = (x * 2654435761u) ^ (y * 40503u) ^ (channel * 977u);
    return (int32_t)((hash >> 7) % 256);
}

static uint32_t v_max_of(const Shape *shape)
{
    uint32_t v_max = 1;
    for (uint32_t channel = 0; channel < 3; channel++) {
        v_max = shape->v[channel] > v_max ? shape->v[channel] : v_max;
    }
    return v_max;
}

static uint32_t h_max_of(const Shape *shape)
{
    uint32_t h_max = 1;
    for (uint32_t channel = 0; channel < 3; channel++) {
        h_max = shape->h[channel] > h_max ? shape->h[channel] : h_max;
    }
    return h_max;
}

static void feed_block(CoverGrid *grid, const Shape *shape, uint32_t channel, uint32_t block_x,
                       uint32_t block_y)
{
    uint32_t width = BLOCK * h_max_of(shape) / shape->h[channel];
    uint32_t height = BLOCK * v_max_of(shape) / shape->v[channel];
    CoverRect rect = {block_x * width, block_y * height, width, height};
    if (cover_grid_touches(grid, rect)) {
        cover_grid_add(grid, channel, rect, value_at(channel, block_x, block_y));
    }
}

static void feed_mcu(CoverGrid *grid, const Shape *shape, uint32_t mcu_x, uint32_t mcu_y)
{
    for (uint32_t channel = 0; channel < 3; channel++) {
        for (uint32_t v = 0; v < shape->v[channel]; v++) {
            for (uint32_t h = 0; h < shape->h[channel]; h++) {
                feed_block(grid, shape, channel, mcu_x * shape->h[channel] + h,
                           mcu_y * shape->v[channel] + v);
            }
        }
    }
}

static bool feed_jpeg_order(CoverGrid *grid, const Shape *shape)
{
    uint32_t band = BLOCK * v_max_of(shape);
    uint32_t mcu_width = BLOCK * h_max_of(shape);
    uint32_t mcus_wide = (shape->width + mcu_width - 1) / mcu_width;
    uint32_t mcus_high = (shape->height + band - 1) / band;
    for (uint32_t channel = 0; channel < 3; channel++) {
        cover_grid_set_sampling(grid, channel, h_max_of(shape) / shape->h[channel],
                                v_max_of(shape) / shape->v[channel]);
    }
    for (uint32_t mcu_y = 0; mcu_y < mcus_high; mcu_y++) {
        for (uint32_t mcu_x = 0; mcu_x < mcus_wide; mcu_x++) {
            feed_mcu(grid, shape, mcu_x, mcu_y);
        }
        if (!cover_grid_reach(grid, (mcu_y + 1) * band, COVER_COLOR_YCBCR)) {
            return false;
        }
    }
    return cover_grid_reach(grid, UINT32_MAX, COVER_COLOR_YCBCR);
}

static bool feed_png_order(CoverGrid *grid, const Shape *shape)
{
    for (uint32_t y = 0; y < shape->height; y++) {
        cover_grid_row_start(grid);
        for (uint32_t x = grid->start_x; x < grid->start_x + grid->span; x++) {
            int32_t values[3] = {value_at(0, x, y), value_at(1, x, y), value_at(2, x, y)};
            cover_grid_row_pixel(grid, x, values, 3);
        }
        cover_grid_row_finish(grid, y, 3);
        if (!cover_grid_reach(grid, y + 1, COVER_COLOR_RGB)) {
            return false;
        }
    }
    return cover_grid_reach(grid, UINT32_MAX, COVER_COLOR_RGB);
}

static CoverGrid *full_grid(const Shape *shape)
{
    CoverGrid *grid = malloc(cover_grid_bytes(COVER_GRID_SIZE));
    cover_grid_begin(grid, COVER_GRID_SIZE);
    CHECK(cover_grid_init(grid, shape->width, shape->height));
    return grid;
}

static CoverGrid *rolling_grid(CoverGridRolling *rolling, const Shape *shape, CoverRowSink sink,
                               uint8_t **memory)
{
    uint32_t size = cover_grid_bytes(COVER_ROLLING_ROWS) + 64;
    *memory = malloc(size);
    ImageArena arena = image_arena_make(*memory, size);
    CoverGrid *grid = cover_grid_rolling_open(rolling, &arena, sink);
    CHECK(grid != NULL);
    rolling->yield = test_yield;
    CHECK(cover_grid_init(grid, shape->width, shape->height));
    return grid;
}

typedef bool (*Feed)(CoverGrid *grid, const Shape *shape);

static void compare(const Shape *shape, Feed feed, CoverColor color)
{
    static uint16_t full[COVER_GRID_CELLS];
    static uint16_t half[COVER_GRID_HALF_CELLS];
    static Collected collected;
    uint8_t *memory = NULL;
    CoverGridRolling *rolling = malloc(sizeof *rolling);
    CoverGrid *whole = full_grid(shape);
    memset(&collected, 0, sizeof collected);
    collected.stop_after = UINT32_MAX;
    CoverRowSink sink = {take_full, take_half, &collected};
    CoverGrid *rows = rolling_grid(rolling, shape, sink, &memory);
    CHECK(feed(whole, shape));
    CHECK(feed(rows, shape));
    cover_grid_render(whole, color, full, half);
    CHECK_EQUAL(FULL, collected.full_rows);
    CHECK_EQUAL(HALF, collected.half_rows);
    CHECK(memcmp(full, collected.full, sizeof full) == 0);
    CHECK(memcmp(half, collected.half, sizeof half) == 0);
    free(whole);
    free(memory);
    free(rolling);
}

static void jpeg_444_matches_the_full_grid(void)
{
    compare(&(Shape){224, 224, {1, 1, 1}, {1, 1, 1}}, feed_jpeg_order, COVER_COLOR_YCBCR);
    compare(&(Shape){640, 480, {1, 1, 1}, {1, 1, 1}}, feed_jpeg_order, COVER_COLOR_YCBCR);
}

static void jpeg_422_matches_the_full_grid(void)
{
    compare(&(Shape){336, 300, {2, 1, 1}, {1, 1, 1}}, feed_jpeg_order, COVER_COLOR_YCBCR);
}

static void jpeg_420_matches_the_full_grid(void)
{
    compare(&(Shape){480, 480, {2, 1, 1}, {2, 1, 1}}, feed_jpeg_order, COVER_COLOR_YCBCR);
    compare(&(Shape){700, 500, {2, 1, 1}, {2, 1, 1}}, feed_jpeg_order, COVER_COLOR_YCBCR);
}

static void jpeg_411_matches_the_full_grid(void)
{
    compare(&(Shape){512, 400, {4, 1, 1}, {1, 1, 1}}, feed_jpeg_order, COVER_COLOR_YCBCR);
}

static void off_centre_crops_match_the_full_grid(void)
{
    compare(&(Shape){1000, 400, {2, 1, 1}, {1, 1, 1}}, feed_jpeg_order, COVER_COLOR_YCBCR);
    compare(&(Shape){300, 900, {1, 1, 1}, {1, 1, 1}}, feed_jpeg_order, COVER_COLOR_YCBCR);
}

static void png_rows_match_the_full_grid(void)
{
    compare(&(Shape){112, 112, {1, 1, 1}, {1, 1, 1}}, feed_png_order, COVER_COLOR_RGB);
    compare(&(Shape){300, 250, {1, 1, 1}, {1, 1, 1}}, feed_png_order, COVER_COLOR_RGB);
    compare(&(Shape){90, 600, {1, 1, 1}, {1, 1, 1}}, feed_png_order, COVER_COLOR_RGB);
}

static void the_sink_can_cancel_the_decode(void)
{
    static Collected collected;
    uint8_t *memory = NULL;
    Shape shape = {640, 480, {1, 1, 1}, {1, 1, 1}};
    CoverGridRolling *rolling = malloc(sizeof *rolling);
    memset(&collected, 0, sizeof collected);
    collected.stop_after = 10;
    CoverRowSink sink = {take_full, take_half, &collected};
    CoverGrid *grid = rolling_grid(rolling, &shape, sink, &memory);
    CHECK(!feed_png_order(grid, &shape));
    CHECK_EQUAL(10, collected.full_rows);
    free(memory);
    free(rolling);
}

static void yielding_between_rows_keeps_the_output(void)
{
    CoverYield yield = start_yield(0);
    test_yield = &yield;
    compare(&(Shape){480, 480, {2, 1, 1}, {2, 1, 1}}, feed_jpeg_order, COVER_COLOR_YCBCR);
    compare(&(Shape){300, 250, {1, 1, 1}, {1, 1, 1}}, feed_png_order, COVER_COLOR_RGB);
    test_yield = NULL;
    CHECK(tick.services > FULL);
}

static void a_refusal_while_preparing_the_totals_stops_before_any_row(void)
{
    static Collected collected;
    uint8_t *memory = NULL;
    Shape shape = {640, 480, {1, 1, 1}, {1, 1, 1}};
    CoverGridRolling *rolling = malloc(sizeof *rolling);
    CoverYield yield = start_yield(3);
    test_yield = &yield;
    memset(&collected, 0, sizeof collected);
    collected.stop_after = UINT32_MAX;
    CoverRowSink sink = {take_full, take_half, &collected};
    CoverGrid *grid = rolling_grid(rolling, &shape, sink, &memory);
    CHECK(!feed_png_order(grid, &shape));
    CHECK_EQUAL(0, collected.full_rows);
    CHECK(yield.cancelled);
    test_yield = NULL;
    free(memory);
    free(rolling);
}

static void a_refusal_between_rows_stops_the_flush(void)
{
    static Collected collected;
    uint8_t *memory = NULL;
    Shape shape = {640, 480, {1, 1, 1}, {1, 1, 1}};
    CoverGridRolling *rolling = malloc(sizeof *rolling);
    CoverYield yield = start_yield(20);
    test_yield = &yield;
    memset(&collected, 0, sizeof collected);
    collected.stop_after = UINT32_MAX;
    CoverRowSink sink = {take_full, take_half, &collected};
    CoverGrid *grid = rolling_grid(rolling, &shape, sink, &memory);
    CHECK(!feed_png_order(grid, &shape));
    CHECK(collected.full_rows > 0 && collected.full_rows < FULL);
    test_yield = NULL;
    free(memory);
    free(rolling);
}

static void the_window_fits_only_what_nine_rows_can_cover(void)
{
    CHECK(cover_grid_rolling_fits(112, 112, 8));
    CHECK(!cover_grid_rolling_fits(111, 111, 8));
    CHECK(cover_grid_rolling_fits(224, 224, 16));
    CHECK(!cover_grid_rolling_fits(223, 223, 16));
    CHECK(cover_grid_rolling_fits(500, 2000, 16));
    CHECK(cover_grid_rolling_fits(112, 112, 1));
    CHECK(cover_grid_rolling_fits(16, 16, 1));
    CHECK(!cover_grid_rolling_fits(8, 8, 1));
    CHECK(!cover_grid_rolling_fits(0, 10, 1));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(jpeg_444_matches_the_full_grid),
        TEST_CASE(jpeg_422_matches_the_full_grid),
        TEST_CASE(jpeg_420_matches_the_full_grid),
        TEST_CASE(jpeg_411_matches_the_full_grid),
        TEST_CASE(off_centre_crops_match_the_full_grid),
        TEST_CASE(png_rows_match_the_full_grid),
        TEST_CASE(the_sink_can_cancel_the_decode),
        TEST_CASE(yielding_between_rows_keeps_the_output),
        TEST_CASE(a_refusal_while_preparing_the_totals_stops_before_any_row),
        TEST_CASE(a_refusal_between_rows_stops_the_flush),
        TEST_CASE(the_window_fits_only_what_nine_rows_can_cover),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
