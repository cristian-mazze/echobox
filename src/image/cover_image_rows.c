#include "image/cover_grid_rolling.h"
#include "image/cover_image.h"

#include <stddef.h>

#include "cover_image_run.h"

CoverImageResult cover_image_decode_rows(ImageSource source, const CoverImageMemory *memory,
                                         CoverRowSink sink)
{
    ImageArena arena = image_arena_make(memory->bytes, memory->size);
    CoverGridRolling *rolling = image_arena_take(&arena, sizeof *rolling);
    CoverColor color = COVER_COLOR_GRAY;
    CoverGrid *grid = rolling == NULL ? NULL : cover_grid_rolling_open(rolling, &arena, sink);
    if (grid == NULL) {
        return COVER_IMAGE_NO_MEMORY;
    }
    CoverImageResult result = cover_image_run(source, &arena, grid, &color);
    if (result == COVER_IMAGE_OK && !cover_grid_reach(grid, UINT32_MAX, color)) {
        result = COVER_IMAGE_CANCELLED;
    }
    return result;
}
