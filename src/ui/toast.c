#include "ui/toast.h"

#include "core/deadline.h"
#include "ui/label.h"
#include "ui/text_wrap.h"

enum {
    TOAST_HEIGHT = 24,
    TOAST_BOTTOM_MARGIN = 10,
    TOAST_SIDE_MARGIN = 4,
    LINE_GAP = 2,
    BORDER = 1
};

typedef char ToastLines[TOAST_LINES][TEXT_WRAP_LINE_CAPACITY];

void toast_show(Toast *toast, const char *text, uint32_t now_ms)
{
    *toast = (Toast){.text = text, .shown_ms = now_ms, .visible = true};
}

static int widest_box(const Theme *theme)
{
    return theme->metrics.screen_width - 2 * TOAST_SIDE_MARGIN;
}

static int wrap_toast(const Toast *toast, const Theme *theme, ToastLines lines)
{
    TextWrap wrap = {theme->fonts.body, widest_box(theme) - 2 * theme->metrics.padding};
    return text_wrap_lines(wrap, toast->text, lines, TOAST_LINES);
}

int toast_line_count(const Toast *toast, const Theme *theme)
{
    ToastLines lines;
    return wrap_toast(toast, theme, lines);
}

void toast_update(Toast *toast, const Theme *theme, uint32_t now_ms)
{
    if (!toast->visible) {
        return;
    }
    bool wrapped = toast_line_count(toast, theme) > 1;
    uint32_t duration = wrapped ? TOAST_LONG_DURATION_MS : TOAST_DURATION_MS;
    if (deadline_reached(now_ms, toast->shown_ms + duration)) {
        toast->visible = false;
    }
}

static int widest_line(const Theme *theme, ToastLines lines, int count)
{
    int widest = 0;
    for (int index = 0; index < count; index++) {
        int width = text_width(theme->fonts.body, lines[index]);
        widest = width > widest ? width : widest;
    }
    return widest;
}

static Rect area_for(const Theme *theme, ToastLines lines, int count)
{
    const ThemeMetrics *metrics = &theme->metrics;
    int width = widest_line(theme, lines, count) + 2 * metrics->padding;
    width = width < widest_box(theme) ? width : widest_box(theme);
    int extra = count > 1 ? (count - 1) * (font_line_height(theme->fonts.body) + LINE_GAP) : 0;
    int height = TOAST_HEIGHT + extra;
    height = height < metrics->screen_height ? height : metrics->screen_height;
    int bottom_margin =
        metrics->screen_height > height + TOAST_BOTTOM_MARGIN ? TOAST_BOTTOM_MARGIN : 0;
    return (Rect){(metrics->screen_width - width) / 2,
                  metrics->screen_height - height - bottom_margin, width, height};
}

Rect toast_area(const Toast *toast, const Theme *theme)
{
    ToastLines lines;
    int count = wrap_toast(toast, theme, lines);
    return area_for(theme, lines, count);
}

static void draw_lines(Canvas *canvas, const Theme *theme, Rect inner, ToastLines lines, int count)
{
    TextStyle style = {.font = theme->fonts.body, .color = theme->colors.header_text};
    for (int index = 0; index < count; index++) {
        int top = inner.y + inner.height * index / count;
        int bottom = inner.y + inner.height * (index + 1) / count;
        Rect row = {inner.x, top, inner.width, bottom - top};
        Rect text_area = label_centered_area(style.font, row, lines[index]);
        label_draw(canvas, style, (Label){.area = text_area, .text = lines[index]});
    }
}

void toast_draw(const Toast *toast, Canvas *canvas, const Theme *theme)
{
    if (!toast->visible) {
        return;
    }
    ToastLines lines;
    int count = wrap_toast(toast, theme, lines);
    Rect area = area_for(theme, lines, count);
    Rect inner = {area.x + BORDER, area.y + BORDER, area.width - 2 * BORDER,
                  area.height - 2 * BORDER};
    canvas_fill_rect(canvas, area, theme->colors.divider);
    canvas_fill_rect(canvas, inner, theme->colors.header_background);
    draw_lines(canvas, theme, inner, lines, count);
}
