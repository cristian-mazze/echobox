#include "image/cover_image.h"

#include "cover_image_run.h"

#include <stddef.h>

enum {
    JPEG_FIRST = 0xFF,
    JPEG_SECOND = 0xD8,
    PNG_FIRST = 0x89,
    PNG_SECOND = 'P'
};

static CoverImageResult decode_any(ImageReader *reader, ImageArena *arena, CoverImage *image)
{
    uint8_t first = image_reader_peek(reader, 0);
    uint8_t second = image_reader_peek(reader, 1);
    if (first == JPEG_FIRST && second == JPEG_SECOND) {
        return jpeg_decode(reader, arena, image);
    }
    if (first == PNG_FIRST && second == PNG_SECOND) {
        return png_decode(reader, arena, image);
    }
    return reader->failed ? COVER_IMAGE_CORRUPT : COVER_IMAGE_UNSUPPORTED;
}

CoverImageResult cover_image_run(ImageSource source, ImageArena *arena, CoverGrid *grid,
                                 CoverColor *color)
{
    ImageReader reader;
    CoverImage image = {.grid = grid, .color = COVER_COLOR_GRAY};
    if (!image_reader_init(&reader, source, arena)) {
        return COVER_IMAGE_NO_MEMORY;
    }
    CoverImageResult result = decode_any(&reader, arena, &image);
    *color = image.color;
    return result;
}

CoverImageResult cover_image_decode(ImageSource source, void *memory, uint32_t size, uint16_t *full,
                                    uint16_t *half)
{
    ImageArena arena = image_arena_make(memory, size);
    CoverGrid *grid = image_arena_take(&arena, cover_grid_bytes(COVER_GRID_SIZE));
    CoverColor color = COVER_COLOR_GRAY;
    if (grid == NULL) {
        return COVER_IMAGE_NO_MEMORY;
    }
    cover_grid_begin(grid, COVER_GRID_SIZE);
    CoverImageResult result = cover_image_run(source, &arena, grid, &color);
    if (result == COVER_IMAGE_OK) {
        cover_grid_render(grid, color, full, half);
    }
    return result;
}
