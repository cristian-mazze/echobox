#pragma once

#include "image/cover_image.h"

typedef struct CoverImageRun {
    ImageSource source;
    ImageArena *arena;
    CoverGrid *grid;
    CoverYield *yield;
} CoverImageRun;

CoverImageResult cover_image_run(const CoverImageRun *run, CoverColor *color);
