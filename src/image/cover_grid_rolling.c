#include "image/cover_grid_rolling.h"

#include <stddef.h>

#include "cover_render.h"

enum {
    CELLS = COVER_GRID_SIZE,
    HALF = COVER_GRID_HALF
};

bool cover_grid_rolling_fits(uint32_t width, uint32_t height, uint32_t band_height)
{
    uint32_t span = width < height ? width : height;
    return cover_grid_band_fits(span, band_height, COVER_ROLLING_ROWS);
}

static uint32_t rows_ready(const CoverGrid *grid, uint32_t y)
{
    if (y >= grid->start_y + grid->span) {
        return CELLS;
    }
    if (y <= grid->start_y) {
        return 0;
    }
    return (y - grid->start_y) * CELLS / grid->span;
}

static bool send_half(CoverGridRolling *rolling, CoverColor color, uint32_t row)
{
    CoverGrid *grid = rolling->grid;
    CoverRowCells cells = cover_grid_row_cells(grid, row - 1);
    uint16_t pixels[HALF];
    for (uint32_t channel = 0; channel < COVER_GRID_CHANNELS; channel++) {
        cells.top[channel] = rolling->held[channel];
    }
    cover_render_half_row(&cells, color, pixels);
    return rolling->sink.half_row == NULL ||
           rolling->sink.half_row(rolling->sink.context, row / 2, pixels);
}

static void hold_row(CoverGridRolling *rolling, uint32_t row)
{
    for (uint32_t channel = 0; channel < COVER_GRID_CHANNELS; channel++) {
        const int32_t *cells = cover_grid_cell_row(rolling->grid, channel, row);
        for (uint32_t cell = 0; cell < CELLS; cell++) {
            rolling->held[channel][cell] = cells[cell];
        }
    }
}

static void clear_row(CoverGridRolling *rolling, uint32_t row)
{
    for (uint32_t channel = 0; channel < COVER_GRID_CHANNELS; channel++) {
        int32_t *cells = cover_grid_cell_row(rolling->grid, channel, row);
        for (uint32_t cell = 0; cell < CELLS; cell++) {
            cells[cell] = 0;
        }
    }
}

static bool flush_row(CoverGridRolling *rolling, CoverColor color, uint32_t row)
{
    uint16_t pixels[CELLS];
    cover_grid_render_row(rolling->grid, color, row, pixels);
    bool accepted = rolling->sink.full_row == NULL ||
                    rolling->sink.full_row(rolling->sink.context, row, pixels);
    if (accepted && row % 2 == 0) {
        hold_row(rolling, row);
    } else if (accepted) {
        accepted = send_half(rolling, color, row);
    }
    clear_row(rolling, row);
    return accepted;
}

static bool rolling_reach(CoverGrid *grid, uint32_t y, CoverColor color)
{
    CoverGridRolling *rolling = grid->reach_context;
    if (!rolling->prepared) {
        cover_grid_prepare_render(grid, color);
        rolling->prepared = true;
    }
    uint32_t ready = rows_ready(grid, y);
    while (rolling->next_row < ready) {
        if (!flush_row(rolling, color, rolling->next_row)) {
            return false;
        }
        rolling->next_row++;
    }
    return true;
}

CoverGrid *cover_grid_rolling_open(CoverGridRolling *rolling, ImageArena *arena, CoverRowSink sink)
{
    CoverGrid *grid = image_arena_take(arena, cover_grid_bytes(COVER_ROLLING_ROWS));
    if (grid == NULL) {
        return NULL;
    }
    cover_grid_begin(grid, COVER_ROLLING_ROWS);
    grid->reach = rolling_reach;
    grid->reach_context = rolling;
    rolling->grid = grid;
    rolling->sink = sink;
    rolling->yield = NULL;
    rolling->next_row = 0;
    rolling->prepared = false;
    return grid;
}
