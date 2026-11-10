#pragma once

#include "image/cover_image.h"

CoverImageResult cover_image_run(ImageSource source, ImageArena *arena, CoverGrid *grid,
                                 CoverColor *color);
