#include "image/cover_grid.h"

#include <stddef.h>

#include "cover_render.h"

enum {
    CELLS = COVER_GRID_SIZE,
    HALF = COVER_GRID_HALF
};

typedef struct CoverSpan {
    uint32_t first;
    uint32_t count;
    uint16_t weights[CELLS];
} CoverSpan;

void cover_grid_begin(CoverGrid *grid, uint32_t ring_rows)
{
    grid->ring_rows = ring_rows;
    grid->reach = NULL;
    grid->reach_context = NULL;
}

int32_t *cover_grid_cell_row(CoverGrid *grid, uint32_t channel, uint32_t row)
{
    return grid->sums + (channel * grid->ring_rows + row % grid->ring_rows) * CELLS;
}

bool cover_grid_reach(CoverGrid *grid, uint32_t y, CoverColor color)
{
    return grid->reach == NULL || grid->reach(grid, y, color);
}

bool cover_grid_band_fits(uint32_t span, uint32_t band_height, uint32_t ring_rows)
{
    if (span == 0 || span > COVER_GRID_LIMIT || band_height == 0 ||
        band_height > COVER_GRID_LIMIT) {
        return false;
    }
    return (span - 1 + band_height * CELLS - 1) / span + 1 <= ring_rows;
}

bool cover_grid_streams(const CoverGrid *grid)
{
    return grid->reach != NULL;
}

bool cover_grid_stream_ok(const CoverGrid *grid, uint32_t band_height)
{
    return !cover_grid_streams(grid) ||
           cover_grid_band_fits(grid->span, band_height, grid->ring_rows);
}

bool cover_grid_init(CoverGrid *grid, uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0 || width > COVER_GRID_LIMIT || height > COVER_GRID_LIMIT) {
        return false;
    }
    grid->span = width < height ? width : height;
    grid->start_x = (width - grid->span) / 2;
    grid->start_y = (height - grid->span) / 2;
    for (uint32_t channel = 0; channel < COVER_GRID_CHANNELS; channel++) {
        grid->sample_width[channel] = 1;
        grid->sample_height[channel] = 1;
        int32_t *cells = cover_grid_cell_row(grid, channel, 0);
        for (uint32_t cell = 0; cell < grid->ring_rows * CELLS; cell++) {
            cells[cell] = 0;
        }
    }
    return true;
}

void cover_grid_set_sampling(CoverGrid *grid, uint32_t channel, uint32_t width, uint32_t height)
{
    grid->sample_width[channel] = width;
    grid->sample_height[channel] = height;
}

static void axis_span(uint32_t start, uint32_t span, uint32_t position, uint32_t length,
                      CoverSpan *out)
{
    uint32_t window_low = start * CELLS;
    uint32_t window_high = window_low + span * CELLS;
    uint32_t low = position * CELLS;
    uint32_t high = (position + length) * CELLS;
    low = low > window_low ? low : window_low;
    high = high < window_high ? high : window_high;
    out->count = 0;
    if (low >= high) {
        return;
    }
    out->first = (low - window_low) / span;
    uint32_t last = (high - window_low - 1) / span;
    for (uint32_t cell = out->first; cell <= last; cell++) {
        uint32_t cell_low = window_low + cell * span;
        uint32_t cell_high = cell_low + span;
        uint32_t from = low > cell_low ? low : cell_low;
        uint32_t to = high < cell_high ? high : cell_high;
        out->weights[out->count++] = (uint16_t)((to - from) * COVER_GRID_WEIGHT_ONE / span);
    }
}

bool cover_grid_touches(const CoverGrid *grid, CoverRect rect)
{
    uint32_t end_x = grid->start_x + grid->span;
    uint32_t end_y = grid->start_y + grid->span;
    return rect.x < end_x && rect.x + rect.width > grid->start_x && rect.y < end_y &&
           rect.y + rect.height > grid->start_y;
}

static void add_spans(CoverGrid *grid, uint32_t channel, const CoverSpan *x, const CoverSpan *y,
                      int32_t value)
{
    for (uint32_t row = 0; row < y->count; row++) {
        int32_t *sums = cover_grid_cell_row(grid, channel, y->first + row) + x->first;
        int32_t row_value = value * (int32_t)y->weights[row];
        for (uint32_t column = 0; column < x->count; column++) {
            int32_t product = row_value * (int32_t)x->weights[column];
            sums[column] = (int32_t)((uint32_t)sums[column] + (uint32_t)product);
        }
    }
}

void cover_grid_add(CoverGrid *grid, uint32_t channel, CoverRect rect, int32_t value)
{
    CoverSpan x;
    CoverSpan y;
    axis_span(grid->start_y, grid->span, rect.y, rect.height, &y);
    if (y.count == 0) {
        return;
    }
    axis_span(grid->start_x, grid->span, rect.x, rect.width, &x);
    add_spans(grid, channel, &x, &y, value);
}

void cover_grid_add_color(CoverGrid *grid, CoverRect rect, const int32_t *values, uint32_t channels)
{
    CoverSpan x;
    CoverSpan y;
    axis_span(grid->start_y, grid->span, rect.y, rect.height, &y);
    if (y.count == 0) {
        return;
    }
    axis_span(grid->start_x, grid->span, rect.x, rect.width, &x);
    for (uint32_t channel = 0; channel < channels; channel++) {
        add_spans(grid, channel, &x, &y, values[channel]);
    }
}

void cover_grid_row_start(CoverGrid *grid)
{
    for (uint32_t channel = 0; channel < COVER_GRID_CHANNELS; channel++) {
        for (uint32_t cell = 0; cell < CELLS; cell++) {
            grid->row[channel][cell] = 0;
        }
    }
}

static void row_add(CoverGrid *grid, uint32_t cell, uint32_t weight, const int32_t *values,
                    uint32_t channels)
{
    for (uint32_t channel = 0; channel < channels; channel++) {
        grid->row[channel][cell] += values[channel] * (int32_t)weight;
    }
}

static void row_add_span(CoverGrid *grid, const CoverSpan *span, const int32_t *values,
                         uint32_t channels)
{
    for (uint32_t cell = 0; cell < span->count; cell++) {
        row_add(grid, span->first + cell, span->weights[cell], values, channels);
    }
}

void cover_grid_row_pixel(CoverGrid *grid, uint32_t x, const int32_t *values, uint32_t channels)
{
    uint32_t span = grid->span;
    if (span < CELLS) {
        CoverSpan cells;
        axis_span(grid->start_x, span, x, 1, &cells);
        row_add_span(grid, &cells, values, channels);
        return;
    }
    uint32_t low = (x - grid->start_x) * CELLS;
    uint32_t cell = low / span;
    uint32_t boundary = (cell + 1) * span;
    uint32_t high = low + CELLS;
    if (high <= boundary) {
        row_add(grid, cell, CELLS * COVER_GRID_WEIGHT_ONE / span, values, channels);
        return;
    }
    row_add(grid, cell, (boundary - low) * COVER_GRID_WEIGHT_ONE / span, values, channels);
    row_add(grid, cell + 1, (high - boundary) * COVER_GRID_WEIGHT_ONE / span, values, channels);
}

void cover_grid_row_finish(CoverGrid *grid, uint32_t y, uint32_t channels)
{
    CoverSpan rows;
    axis_span(grid->start_y, grid->span, y, 1, &rows);
    for (uint32_t row = 0; row < rows.count; row++) {
        for (uint32_t channel = 0; channel < channels; channel++) {
            int32_t *sums = cover_grid_cell_row(grid, channel, rows.first + row);
            const int32_t *values = grid->row[channel];
            for (uint32_t cell = 0; cell < CELLS; cell++) {
                int32_t product = values[cell] * (int32_t)rows.weights[row];
                sums[cell] = (int32_t)((uint32_t)sums[cell] + (uint32_t)product);
            }
        }
    }
}

static void axis_totals(uint32_t start, uint32_t span, uint32_t sample, uint32_t *totals)
{
    CoverSpan cells;
    for (uint32_t cell = 0; cell < CELLS; cell++) {
        totals[cell] = 0;
    }
    for (uint32_t index = start / sample; index * sample < start + span; index++) {
        axis_span(start, span, index * sample, sample, &cells);
        for (uint32_t cell = 0; cell < cells.count; cell++) {
            totals[cells.first + cell] += cells.weights[cell];
        }
    }
}

uint32_t cover_grid_prepare_steps(CoverColor color)
{
    return 2 * (color == COVER_COLOR_GRAY ? 1 : COVER_GRID_CHANNELS);
}

void cover_grid_prepare_step(CoverGrid *grid, uint32_t step)
{
    uint32_t channel = step / 2;
    if (step % 2 == 0) {
        axis_totals(grid->start_x, grid->span, grid->sample_width[channel],
                    grid->totals_x[channel]);
        return;
    }
    axis_totals(grid->start_y, grid->span, grid->sample_height[channel], grid->totals_y[channel]);
}

void cover_grid_prepare_render(CoverGrid *grid, CoverColor color)
{
    for (uint32_t step = 0; step < cover_grid_prepare_steps(color); step++) {
        cover_grid_prepare_step(grid, step);
    }
}

CoverRowCells cover_grid_row_cells(CoverGrid *grid, uint32_t cell_row)
{
    CoverRowCells cells;
    uint32_t below = cell_row + 1 < CELLS ? cell_row + 1 : cell_row;
    for (uint32_t channel = 0; channel < COVER_GRID_CHANNELS; channel++) {
        cells.top[channel] = cover_grid_cell_row(grid, channel, cell_row);
        cells.bottom[channel] = cover_grid_cell_row(grid, channel, below);
        cells.totals_x[channel] = grid->totals_x[channel];
        cells.totals_top[channel] = grid->totals_y[channel][cell_row];
        cells.totals_bottom[channel] = grid->totals_y[channel][below];
    }
    return cells;
}

void cover_grid_render_row(CoverGrid *grid, CoverColor color, uint32_t y, uint16_t *out)
{
    CoverRowCells cells = cover_grid_row_cells(grid, y);
    cover_render_full_row(&cells, color, out);
}

void cover_grid_render_half_row(CoverGrid *grid, CoverColor color, uint32_t y, uint16_t *out)
{
    CoverRowCells cells = cover_grid_row_cells(grid, 2 * y);
    cover_render_half_row(&cells, color, out);
}

void cover_grid_render(CoverGrid *grid, CoverColor color, uint16_t *full, uint16_t *half)
{
    cover_grid_prepare_render(grid, color);
    for (uint32_t y = 0; y < CELLS; y++) {
        cover_grid_render_row(grid, color, y, full + y * CELLS);
    }
    for (uint32_t y = 0; y < HALF; y++) {
        cover_grid_render_half_row(grid, color, y, half + y * HALF);
    }
}
