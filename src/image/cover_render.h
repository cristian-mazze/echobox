#pragma once

#include <stdint.h>

#include "image/cover_grid.h"

typedef struct CoverRowCells {
    const int32_t *top[COVER_GRID_CHANNELS];
    const int32_t *bottom[COVER_GRID_CHANNELS];
    const uint32_t *totals_x[COVER_GRID_CHANNELS];
    uint32_t totals_top[COVER_GRID_CHANNELS];
    uint32_t totals_bottom[COVER_GRID_CHANNELS];
} CoverRowCells;

void cover_render_full_row(const CoverRowCells *cells, CoverColor color, uint16_t *out);
void cover_render_half_row(const CoverRowCells *cells, CoverColor color, uint16_t *out);

CoverRowCells cover_grid_row_cells(CoverGrid *grid, uint32_t cell_row);
