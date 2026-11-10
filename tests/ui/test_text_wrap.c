#include <string.h>

#include "check.h"
#include "tiny_font.h"
#include "ui/text_wrap.h"

enum {
    LINES = 4
};

static char lines[LINES][TEXT_WRAP_LINE_CAPACITY];

static int wrap(const char *text, int width, int max_lines)
{
    return text_wrap_lines((TextWrap){&TINY_FONT, width}, text, lines, max_lines);
}

static void text_that_fits_stays_on_one_line(void)
{
    CHECK_EQUAL(1, wrap("AB AB", 100, LINES));
    CHECK(strcmp("AB AB", lines[0]) == 0);
}

static void lines_break_between_words(void)
{
    CHECK_EQUAL(3, wrap("AB AB AB", 9, LINES));
    CHECK(strcmp("AB", lines[0]) == 0);
    CHECK(strcmp("AB", lines[1]) == 0);
    CHECK(strcmp("AB", lines[2]) == 0);
}

static void as_many_words_as_fit_share_a_line(void)
{
    CHECK_EQUAL(2, wrap("AB AB AB", 16, LINES));
    CHECK(strcmp("AB AB", lines[0]) == 0);
    CHECK(strcmp("AB", lines[1]) == 0);
}

static void a_word_wider_than_the_line_breaks_between_characters(void)
{
    CHECK_EQUAL(3, wrap("AAAAAA", 8, LINES));
    CHECK(strcmp("AA", lines[0]) == 0);
    CHECK(strcmp("AA", lines[1]) == 0);
    CHECK(strcmp("AA", lines[2]) == 0);
}

static void a_break_never_splits_a_character(void)
{
    static const char TEXT[] = "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9";
    CHECK_EQUAL(3, wrap(TEXT, 5, LINES));
    CHECK(strcmp("\xC3\xA9\xC3\xA9", lines[0]) == 0);
    CHECK(strcmp("\xC3\xA9\xC3\xA9", lines[1]) == 0);
    CHECK(strcmp("\xC3\xA9", lines[2]) == 0);
}

static void a_narrow_line_still_takes_one_character(void)
{
    CHECK_EQUAL(2, wrap("AA", 1, LINES));
    CHECK(strcmp("A", lines[0]) == 0);
    CHECK(strcmp("A", lines[1]) == 0);
}

static void the_last_allowed_line_takes_the_rest(void)
{
    CHECK_EQUAL(2, wrap("AB AB AB AB", 9, 2));
    CHECK(strcmp("AB", lines[0]) == 0);
    CHECK(strcmp("AB AB AB", lines[1]) == 0);
}

static void empty_text_has_no_lines(void)
{
    CHECK_EQUAL(0, wrap("", 10, LINES));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(text_that_fits_stays_on_one_line),
        TEST_CASE(lines_break_between_words),
        TEST_CASE(as_many_words_as_fit_share_a_line),
        TEST_CASE(a_word_wider_than_the_line_breaks_between_characters),
        TEST_CASE(a_break_never_splits_a_character),
        TEST_CASE(a_narrow_line_still_takes_one_character),
        TEST_CASE(the_last_allowed_line_takes_the_rest),
        TEST_CASE(empty_text_has_no_lines),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
