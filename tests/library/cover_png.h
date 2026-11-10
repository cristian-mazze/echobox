#pragma once

#include <stdint.h>

enum {
    COVER_PNG_SIDE = 112,
    COVER_PNG_ROW = 1 + COVER_PNG_SIDE * 3,
    COVER_PNG_RAW = COVER_PNG_SIDE * COVER_PNG_ROW,
    COVER_PNG_CAPACITY = COVER_PNG_RAW + 128
};

typedef struct Color {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint16_t pixel;
} Color;

uint32_t cover_png_make(uint8_t *png, Color color);
