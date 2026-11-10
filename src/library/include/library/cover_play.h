#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "image/cover_yield.h"
#include "library/cover_build.h"

struct CoverState;
struct ArtFile;

typedef struct CoverPlay {
    CoverYield *yield;
    bool (*may_write)(void *context);
    void *context;
    bool jpeg_only;
    int (*place)(struct CoverState *state, const CoverJob *job, const struct ArtFile *art);
} CoverPlay;

CoverPlay cover_play_make(CoverYield *yield, bool (*may_write)(void *context), void *context);
