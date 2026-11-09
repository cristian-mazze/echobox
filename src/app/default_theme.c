#include "default_theme.h"

#include "fonts/fonts.h"
#include "hal/display.h"
#include "settings/settings.h"

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
            .body = &FONT_TERMINUS_BOLD_16,
            .small = &FONT_TERMINUS_BOLD_12,
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

static const Color ACCENTS[ACCENT_COUNT] = {COLOR_RGB(242, 122, 24), COLOR_RGB(248, 184, 0),
                                            COLOR_RGB(136, 216, 40), COLOR_RGB(0, 200, 216),
                                            COLOR_RGB(88, 160, 248), COLOR_RGB(176, 128, 248),
                                            COLOR_RGB(248, 96, 168), COLOR_RGB(240, 72, 72)};

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

void default_theme_set_accent(Theme *theme, uint8_t accent)
{
    Color color = ACCENTS[accent < ACCENT_COUNT ? accent : 0];
    theme->colors.selection_background = color;
    theme->colors.selection_top = color;
    theme->colors.selection_shine = color;
    theme->colors.success = color;
    theme->colors.chevron = color;
}
