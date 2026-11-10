#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "image/cover_yield.h"
#include "library/cover_build.h"

typedef int (*CoverPlaceFunction)(void *state, const CoverJob *job, const void *art);

typedef struct CoverPlay {
    CoverYield *yield;
    bool (*may_write)(void *context);
    void *context;
    bool jpeg_only;
    CoverPlaceFunction place;
} CoverPlay;

int cover_play_place(void *state, const CoverJob *job, const void *art);

static inline CoverPlay cover_play_make(CoverYield *yield, bool (*may_write)(void *context),
                                        void *context)
{
    return (CoverPlay){.yield = yield,
                       .may_write = may_write,
                       .context = context,
                       .jpeg_only = true,
                       .place = NULL};
}
