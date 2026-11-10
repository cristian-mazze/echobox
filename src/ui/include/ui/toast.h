#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx/canvas.h"
#include "ui/theme.h"

enum {
    TOAST_DURATION_MS = 1500,
    TOAST_LONG_DURATION_MS = 3000,
    TOAST_LINES = 3
};

typedef struct Toast {
    const char *text;
    uint32_t shown_ms;
    bool visible;
} Toast;

void toast_show(Toast *toast, const char *text, uint32_t now_ms);
void toast_update(Toast *toast, const Theme *theme, uint32_t now_ms);
int toast_line_count(const Toast *toast, const Theme *theme);
Rect toast_area(const Toast *toast, const Theme *theme);
void toast_draw(const Toast *toast, Canvas *canvas, const Theme *theme);
