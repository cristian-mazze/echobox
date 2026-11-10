add_library(echo_cover_render STATIC
    cover_render.c
)
target_include_directories(echo_cover_render PUBLIC include)
target_link_libraries(echo_cover_render
    PUBLIC echo_core
    PRIVATE echo_options
)
add_library(echo_image STATIC
    cover_grid.c
    cover_grid_rolling.c
    cover_image.c
    cover_image_rows.c
    image_arena.c
    image_reader.c
    inflate.c
    jpeg.c
    jpeg_blocks.c
    jpeg_entropy.c
    png.c
)
target_include_directories(echo_image PUBLIC include)
target_link_libraries(echo_image
    PUBLIC echo_core echo_cover_render
    PRIVATE echo_options
)
