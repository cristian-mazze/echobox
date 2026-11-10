#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "gfx/font.h"

enum {
    TEXT_WRAP_LINE_CAPACITY = 128
};

typedef struct TextWrap {
    const Font *font;
    int width;
} TextWrap;

int text_wrap_lines(TextWrap wrap, const char *text, char (*lines)[TEXT_WRAP_LINE_CAPACITY],
                    int max_lines);
