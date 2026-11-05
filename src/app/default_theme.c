#include "default_theme.h"

#include "fonts/fonts.h"
#include "hal/display.h"

const Theme DEFAULT_THEME = {
    .colors =
        {
            .background = COLOR_RGB(8, 8, 8),
            .surface = COLOR_RGB(32, 32, 32),
            .text = COLOR_RGB(255, 255, 255),
            .text_secondary = COLOR_RGB(160, 160, 160),
            .text_tertiary = COLOR_RGB(112, 112, 112),
            .selection_background = COLOR_RGB(242, 122, 24),
            .selection_top = COLOR_RGB(242, 122, 24),
            .selection_shine = COLOR_RGB(255, 190, 120),
            .selection_text = COLOR_RGB(8, 8, 8),
            .header_background = COLOR_RGB(32, 32, 32),
            .header_top = COLOR_RGB(32, 32, 32),
            .header_text = COLOR_RGB(255, 255, 255),
            .divider = COLOR_RGB(56, 56, 56),
            .scrollbar_thumb = COLOR_RGB(160, 160, 160),
            .danger = COLOR_RGB(240, 80, 80),
            .success = COLOR_RGB(242, 122, 24),
            .chevron = COLOR_RGB(242, 122, 24),
        },
    .fonts =
        {
            .title = &FONT_TERMINUS_BOLD_16,
            .body = &FONT_TERMINUS_REGULAR_16,
            .small = &FONT_TERMINUS_REGULAR_12,
        },
    .metrics =
        {
            .screen_width = HAL_DISPLAY_WIDTH,
            .screen_height = HAL_DISPLAY_HEIGHT,
            .header_height = 24,
            .header_inset = 68,
            .row_height = 20,
            .padding = 6,
            .scrollbar_width = 3,
            .scrollbar_inset = 0,
            .detail_gap = 8,
            .status_icon_size = 10,
        },
    .marquee = {.pause_ms = 1200, .speed_px_per_s = 40, .gap_px = 32},
};

enum {
    PORTRAIT_STATUS_HEIGHT = 16,
    PORTRAIT_TITLE_HEIGHT = 24
};

Theme default_theme_for(bool portrait)
{
    Theme theme = DEFAULT_THEME;
    if (portrait) {
        theme.metrics.screen_width = HAL_DISPLAY_SHORT_SIDE;
        theme.metrics.screen_height = HAL_DISPLAY_LONG_SIDE;
        theme.metrics.status_height = PORTRAIT_STATUS_HEIGHT;
        theme.metrics.header_height = PORTRAIT_STATUS_HEIGHT + PORTRAIT_TITLE_HEIGHT;
        theme.metrics.header_inset = theme.metrics.padding;
    }
    return theme;
}
