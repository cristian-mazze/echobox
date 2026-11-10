#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct CoverRowSink {
    bool (*full_row)(void *context, uint32_t y, const uint16_t *pixels);
    bool (*half_row)(void *context, uint32_t y, const uint16_t *pixels);
    void *context;
} CoverRowSink;
