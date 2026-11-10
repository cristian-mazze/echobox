#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    COVER_YIELD_SLICE_MS = 2
};

typedef struct CoverYield {
    uint32_t (*now_ms)(void *context);
    bool (*service)(void *context);
    void *context;
    uint32_t slice_ms;
    uint32_t started_ms;
    uint32_t longest_ms;
    bool cancelled;
} CoverYield;

CoverYield cover_yield_make(uint32_t (*now_ms)(void *context), bool (*service)(void *context),
                            void *context);
void cover_yield_begin(CoverYield *yield);
bool cover_yield_serve(CoverYield *yield);

static inline bool cover_yield_point(CoverYield *yield)
{
    return yield == NULL || cover_yield_serve(yield);
}
