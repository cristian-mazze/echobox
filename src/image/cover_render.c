#include "cover_render.h"

enum {
    Q8_ONE = 256,
    BYTE_MAX = 255,
    CHROMA_CENTER_Q8 = 128 * Q8_ONE,
    CR_TO_R = 91881,
    CB_TO_G = 22554,
    CR_TO_G = 46802,
    CB_TO_B = 116130,
    COEFFICIENT_SHIFT = 16
};

static int32_t mean_q8(int64_t sum, uint64_t weight)
{
    if (weight == 0) {
        return 0;
    }
    int64_t scaled = sum * Q8_ONE;
    int64_t half = (int64_t)(weight / 2);
    return (int32_t)(scaled >= 0 ? (scaled + half) / (int64_t)weight
                                 : -((-scaled + half) / (int64_t)weight));
}

static uint8_t to_byte(int32_t q8)
{
    int32_t value = (q8 + Q8_ONE / 2) / Q8_ONE;
    if (q8 < 0) {
        return 0;
    }
    return (uint8_t)(value > BYTE_MAX ? BYTE_MAX : value);
}

static int32_t scaled(int32_t value, int32_t coefficient)
{
    int64_t product = (int64_t)value * coefficient;
    return (int32_t)(product / (1 << COEFFICIENT_SHIFT));
}

static uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    return (uint16_t)((red >> 3) << 11 | (green >> 2) << 5 | (blue >> 3));
}

static uint16_t pixel(CoverColor color, const int32_t *q8)
{
    if (color == COVER_COLOR_GRAY) {
        uint8_t gray = to_byte(q8[0]);
        return rgb565(gray, gray, gray);
    }
    if (color == COVER_COLOR_RGB) {
        return rgb565(to_byte(q8[0]), to_byte(q8[1]), to_byte(q8[2]));
    }
    int32_t cb = q8[1] - CHROMA_CENTER_Q8;
    int32_t cr = q8[2] - CHROMA_CENTER_Q8;
    int32_t red = q8[0] + scaled(cr, CR_TO_R);
    int32_t green = q8[0] - scaled(cb, CB_TO_G) - scaled(cr, CR_TO_G);
    int32_t blue = q8[0] + scaled(cb, CB_TO_B);
    return rgb565(to_byte(red), to_byte(green), to_byte(blue));
}

static uint32_t used_channels(CoverColor color)
{
    return color == COVER_COLOR_GRAY ? 1 : COVER_GRID_CHANNELS;
}

void cover_render_full_row(const CoverRowCells *cells, CoverColor color, uint16_t *out)
{
    int32_t q8[COVER_GRID_CHANNELS] = {0, 0, 0};
    for (uint32_t x = 0; x < COVER_GRID_SIZE; x++) {
        for (uint32_t channel = 0; channel < used_channels(color); channel++) {
            uint64_t weight = (uint64_t)cells->totals_x[channel][x] * cells->totals_top[channel];
            q8[channel] = mean_q8(cells->top[channel][x], weight);
        }
        out[x] = pixel(color, q8);
    }
}

static int64_t half_sum(const CoverRowCells *cells, uint32_t channel, uint32_t x)
{
    const int32_t *top = cells->top[channel];
    const int32_t *bottom = cells->bottom[channel];
    return (int64_t)top[2 * x] + top[2 * x + 1] + bottom[2 * x] + bottom[2 * x + 1];
}

static uint64_t half_weight(const CoverRowCells *cells, uint32_t channel, uint32_t x)
{
    const uint32_t *across = cells->totals_x[channel];
    uint64_t height = (uint64_t)cells->totals_top[channel] + cells->totals_bottom[channel];
    return (uint64_t)(across[2 * x] + across[2 * x + 1]) * height;
}

void cover_render_half_row(const CoverRowCells *cells, CoverColor color, uint16_t *out)
{
    int32_t q8[COVER_GRID_CHANNELS] = {0, 0, 0};
    for (uint32_t x = 0; x < COVER_GRID_HALF; x++) {
        for (uint32_t channel = 0; channel < used_channels(color); channel++) {
            q8[channel] = mean_q8(half_sum(cells, channel, x), half_weight(cells, channel, x));
        }
        out[x] = pixel(color, q8);
    }
}
