#include "ui/message_screen.h"

#include <stddef.h>

#include "ui/header.h"
#include "ui/label.h"
#include "ui/navigation.h"
#include "ui/text_wrap.h"

enum {
    MESSAGE_LINES = 4,
    DETAIL_LINES = 3,
    DETAIL_GAP = 8,
    LINE_GAP = 2
};

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    (void)context;
    (void)now_ms;
    return navigation_exit_action(event);
}

static void update(void *context, uint32_t now_ms)
{
    (void)context;
    (void)now_ms;
}

typedef struct TextBlock {
    char lines[MESSAGE_LINES][TEXT_WRAP_LINE_CAPACITY];
    int count;
    TextStyle style;
} TextBlock;

static void wrap_block(TextBlock *block, const char *text, int width, int max_lines)
{
    block->count = text != NULL ? text_wrap_lines((TextWrap){block->style.font, width}, text,
                                                  block->lines, max_lines)
                                : 0;
}

static int block_height(const TextBlock *block)
{
    return block->count * (font_line_height(block->style.font) + LINE_GAP);
}

static int draw_block(Canvas *canvas, const TextBlock *block, Rect area)
{
    int line_height = font_line_height(block->style.font) + LINE_GAP;
    for (int index = 0; index < block->count; index++) {
        Rect row = {area.x, area.y + index * line_height, area.width, line_height};
        Rect place = label_centered_area(block->style.font, row, block->lines[index]);
        label_draw(canvas, block->style, (Label){.area = place, .text = block->lines[index]});
    }
    return block_height(block);
}

static void draw(void *context, Canvas *canvas)
{
    const MessageScreen *message = context;
    const Theme *theme = message->config.theme;
    Rect content = theme_content_area(theme);
    TextBlock body = {.style = {.font = theme->fonts.body, .color = theme->colors.text_secondary}};
    TextBlock detail = {.style = {.font = theme->fonts.body, .color = theme->colors.text_tertiary}};
    int width = content.width - 2 * theme->metrics.padding;
    canvas_fill_rect(canvas, content, theme->colors.background);
    header_draw(canvas, theme, message->config.title);
    wrap_block(&body, message->config.message, width, MESSAGE_LINES);
    wrap_block(&detail, message->config.detail, width, DETAIL_LINES);
    int gap = detail.count > 0 ? DETAIL_GAP : 0;
    int top = content.y + (content.height - block_height(&body) - gap - block_height(&detail)) / 2;
    Rect area = {content.x + theme->metrics.padding, top, width, 0};
    area.y += draw_block(canvas, &body, area) + gap;
    (void)draw_block(canvas, &detail, area);
}

Screen *message_screen_init(MessageScreen *message, MessageConfig config)
{
    *message = (MessageScreen){.config = config};
    message->screen = (Screen){
        .title = config.title,
        .context = message,
        .enter = NULL,
        .handle_key = handle_key,
        .update = update,
        .draw = draw,
    };
    return &message->screen;
}
