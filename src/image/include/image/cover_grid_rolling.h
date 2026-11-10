#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "image/cover_grid.h"
#include "image/cover_row_sink.h"
#include "image/image_arena.h"

enum {
    COVER_ROLLING_ROWS = 9,
    COVER_ROLLING_WINDOW_BYTES =
        COVER_ROLLING_ROWS * COVER_GRID_SIZE * COVER_GRID_CHANNELS * (int)sizeof(int32_t)
};

typedef struct CoverGridRolling {
    CoverGrid *grid;
    CoverRowSink sink;
    uint32_t next_row;
    bool prepared;
    int32_t held[COVER_GRID_CHANNELS][COVER_GRID_SIZE];
} CoverGridRolling;

_Static_assert(COVER_ROLLING_WINDOW_BYTES == 12096, "the rolling window is 12,096 bytes");
_Static_assert(sizeof(((CoverGridRolling *)0)->held) == 1344, "the held row is 1,344 bytes");

bool cover_grid_rolling_fits(uint32_t width, uint32_t height, uint32_t band_height);
CoverGrid *cover_grid_rolling_open(CoverGridRolling *rolling, ImageArena *arena, CoverRowSink sink);
