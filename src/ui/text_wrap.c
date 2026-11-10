#include "ui/text_wrap.h"

#include "gfx/text.h"

enum {
    CONTINUATION_MASK = 0xC0,
    CONTINUATION_BYTE = 0x80
};

static size_t word_end(const char *text, size_t from)
{
    while (text[from] != '\0' && text[from] != ' ') {
        from++;
    }
    return from;
}

static size_t text_end(const char *text)
{
    size_t end = 0;
    while (text[end] != '\0') {
        end++;
    }
    return end;
}

static void copy_span(char *line, const char *text, size_t count)
{
    size_t length = count < TEXT_WRAP_LINE_CAPACITY - 1 ? count : TEXT_WRAP_LINE_CAPACITY - 1;
    for (size_t index = 0; index < length; index++) {
        line[index] = text[index];
    }
    line[length] = '\0';
}

static bool fits(TextWrap wrap, const char *text, size_t count, char *line)
{
    copy_span(line, text, count);
    return text_width(wrap.font, line) <= wrap.width;
}

static size_t next_character(const char *text, size_t from)
{
    from++;
    while (((unsigned char)text[from] & CONTINUATION_MASK) == CONTINUATION_BYTE) {
        from++;
    }
    return from;
}

static size_t split_word(TextWrap wrap, const char *text, size_t end, char *line)
{
    size_t fitted = next_character(text, 0);
    while (fitted < end && fits(wrap, text, next_character(text, fitted), line)) {
        fitted = next_character(text, fitted);
    }
    return fitted;
}

static size_t whole_words(TextWrap wrap, const char *text, char *line)
{
    size_t fitted = word_end(text, 0);
    while (text[fitted] == ' ') {
        size_t end = word_end(text, fitted + 1);
        if (!fits(wrap, text, end, line)) {
            break;
        }
        fitted = end;
    }
    return fitted;
}

static size_t line_end(TextWrap wrap, const char *text, bool last, char *line)
{
    size_t first = word_end(text, 0);
    if (last || fits(wrap, text, text_end(text), line)) {
        return text_end(text);
    }
    return fits(wrap, text, first, line) ? whole_words(wrap, text, line)
                                         : split_word(wrap, text, first, line);
}

size_t text_wrap_line(TextWrap wrap, const char *text, bool last, char *line)
{
    size_t fitted = line_end(wrap, text, last, line);
    copy_span(line, text, fitted);
    while (text[fitted] == ' ') {
        fitted++;
    }
    return fitted;
}

int text_wrap_lines(TextWrap wrap, const char *text, char (*lines)[TEXT_WRAP_LINE_CAPACITY],
                    int max_lines)
{
    int count = 0;
    while (*text != '\0' && count < max_lines) {
        text += text_wrap_line(wrap, text, count == max_lines - 1, lines[count]);
        count++;
    }
    return count;
}
