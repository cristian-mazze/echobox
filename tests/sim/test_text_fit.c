#include <stdio.h>
#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "core/string_builder.h"
#include "core/utf8.h"
#include "fonts/fonts.h"
#include "hal/display.h"
#include "settings/settings.h"
#include "sim_glyphs.h"
#include "ui/text_wrap.h"
#include "ui/toast.h"

enum {
    PATH_CAPACITY = 1024,
    PACK_LIMIT = 0x3000,
    TEXT_CAPACITY = 256,
    PADDING = 6,
    HEADER_HEIGHT = 24,
    TOAST_SIDE_MARGIN = 4,
    NOTICE_DETAIL_LINES = 5,
    PROBE_LINES = 8
};

typedef struct Sample {
    StringId pattern;
    const char *arguments[2];
    int count;
} Sample;

static const char PACK_PATHS[] = ECHO_LANGUAGE_PACK_PATHS;
static uint8_t pack[PACK_LIMIT + 1];
static int worst_toast;
static int worst_detail;

static uint32_t read_pack(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }
    size_t size = fread(pack, 1, PACK_LIMIT + 1, file);
    fclose(file);
    return (uint32_t)size;
}

static Theme theme_for(int width, int height)
{
    Theme theme = {0};
    theme.fonts.body = &FONT_TERMINUS_BOLD_16;
    theme.metrics.screen_width = width;
    theme.metrics.screen_height = height;
    theme.metrics.header_height = HEADER_HEIGHT;
    theme.metrics.padding = PADDING;
    return theme;
}

static void format_sample(Sample sample, char *text)
{
    StringBuilder builder = string_builder_make(text, TEXT_CAPACITY);
    i18n_format(&builder, sample.pattern, sample.arguments, (uint32_t)sample.count);
}

static bool glyphs_present(const char *text)
{
    const char *cursor = text;
    for (uint32_t codepoint = utf8_next(&cursor); codepoint != 0; codepoint = utf8_next(&cursor)) {
        const Glyph *glyph = font_find_glyph(&FONT_TERMINUS_BOLD_16, codepoint);
        if (glyph == NULL || glyph->codepoint != codepoint) {
            printf("missing glyph U+%04X in \"%s\"\n", codepoint, text);
            return false;
        }
    }
    return true;
}

static int lines_needed(const char *text, int width)
{
    char lines[PROBE_LINES][TEXT_WRAP_LINE_CAPACITY];
    return text_wrap_lines((TextWrap){&FONT_TERMINUS_BOLD_16, width}, text, lines, PROBE_LINES);
}

static void check_toast(const char *text, Theme theme)
{
    int width = theme.metrics.screen_width - 2 * TOAST_SIDE_MARGIN - 2 * PADDING;
    int needed = lines_needed(text, width);
    worst_toast = needed > worst_toast ? needed : worst_toast;
    if (needed > TOAST_LINES) {
        printf("toast %dpx: %d lines: \"%s\"\n", theme.metrics.screen_width, needed, text);
    }
    CHECK(needed <= TOAST_LINES);
}

static void check_detail(const char *text, Theme theme)
{
    int needed = lines_needed(text, theme.metrics.screen_width - 2 * PADDING);
    worst_detail = needed > worst_detail ? needed : worst_detail;
    if (needed > NOTICE_DETAIL_LINES) {
        printf("detail %dpx: %d lines: \"%s\"\n", theme.metrics.screen_width, needed, text);
    }
    CHECK(needed <= NOTICE_DETAIL_LINES);
}

static void check_language(void)
{
    static const Theme *themes[2];
    Theme landscape = theme_for(HAL_DISPLAY_LONG_SIDE, HAL_DISPLAY_SHORT_SIDE);
    Theme portrait = theme_for(HAL_DISPLAY_SHORT_SIDE, HAL_DISPLAY_LONG_SIDE);
    themes[0] = &landscape;
    themes[1] = &portrait;
    char library[TEXT_CAPACITY];
    char text[TEXT_CAPACITY];
    format_sample((Sample){STR_RESCAN_LIBRARY_TRACKS_COVERS, {"99999", "2673"}, 2}, library);
    for (int variant = 0; variant < 2; variant++) {
        StringId more = variant == 0 ? STR_RESCAN_MORE_LATER : STR_RESCAN_MORE_MANUAL;
        StringId first = variant == 0 ? STR_COVERS_FIRST_LATER : STR_COVERS_FIRST_MANUAL;
        for (int orientation = 0; orientation < 2; orientation++) {
            format_sample((Sample){STR_RESCAN_WITH_MORE, {library, i18n_text(more)}, 2}, text);
            CHECK(glyphs_present(text));
            check_toast(text, *themes[orientation]);
            format_sample((Sample){first, {"100", NULL}, 1}, text);
            CHECK(glyphs_present(text));
            check_detail(text, *themes[orientation]);
        }
    }
}

static void the_cover_notices_fit_in_every_language_and_orientation(void)
{
    uint32_t count = 0;
    const char *cursor = PACK_PATHS;
    while (*cursor != '\0') {
        char path[PATH_CAPACITY];
        size_t length = strcspn(cursor, "|");
        memcpy(path, cursor, length);
        path[length] = '\0';
        CHECK(i18n_attach(pack, read_pack(path)));
        check_language();
        i18n_detach();
        count++;
        cursor += length + (cursor[length] == '|' ? 1u : 0u);
    }
    CHECK_EQUAL(LANGUAGE_COUNT, count);
    printf("worst toast %d lines, worst detail %d lines\n", worst_toast, worst_detail);
}

int main(void)
{
    sim_glyphs_attach();
    static const TestCase cases[] = {
        TEST_CASE(the_cover_notices_fit_in_every_language_and_orientation),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
