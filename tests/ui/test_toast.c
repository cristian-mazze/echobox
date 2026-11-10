#include "check.h"
#include "ui/toast.h"
#include "ui_test_canvas.h"
#include "ui_test_theme.h"

enum {
    NOW_MS = 1000,
    TALL_SCREEN_HEIGHT = 120
};

static void a_new_toast_is_hidden(void)
{
    Toast toast = {0};
    CHECK(!toast.visible);
}

static void a_shown_toast_lasts_its_duration(void)
{
    Toast toast = {0};
    toast_show(&toast, "A", NOW_MS);
    toast_update(&toast, &TEST_THEME, NOW_MS + TOAST_DURATION_MS - 1);
    CHECK(toast.visible);
    toast_update(&toast, &TEST_THEME, NOW_MS + TOAST_DURATION_MS);
    CHECK(!toast.visible);
}

static void a_visible_toast_paints_its_box(void)
{
    Toast toast = {0};
    toast_show(&toast, "AB", NOW_MS);
    Canvas canvas = ui_test_canvas();
    toast_draw(&toast, &canvas, &TEST_THEME);
    Rect area = toast_area(&toast, &TEST_THEME);
    CHECK(area.width > 0);
    CHECK(area.y + area.height <= TEST_SCREEN_HEIGHT);
    CHECK(ui_test_area_has_color(&canvas, area, TEST_THEME.colors.header_background));
    CHECK(ui_test_area_has_color(&canvas, area, TEST_THEME.colors.header_text));
}

static void a_hidden_toast_paints_nothing(void)
{
    Toast toast = {0};
    toast_show(&toast, "AB", NOW_MS);
    toast_update(&toast, &TEST_THEME, NOW_MS + TOAST_DURATION_MS);
    Canvas canvas = ui_test_canvas();
    Rect screen = {0, 0, TEST_SCREEN_WIDTH, TEST_SCREEN_HEIGHT};
    toast_draw(&toast, &canvas, &TEST_THEME);
    CHECK(!ui_test_area_has_color(&canvas, screen, TEST_THEME.colors.header_background));
}

static void a_long_toast_wraps_onto_more_lines_and_stays_longer(void)
{
    Toast toast = {0};
    toast_show(&toast, "AB AB AB AB AB AB", NOW_MS);
    Toast short_toast = {0};
    toast_show(&short_toast, "AB", NOW_MS);
    CHECK(toast_line_count(&toast, &TEST_THEME) > 1);
    Theme tall = TEST_THEME;
    tall.metrics.screen_height = TALL_SCREEN_HEIGHT;
    CHECK(toast_area(&toast, &tall).height > toast_area(&short_toast, &tall).height);
    toast_update(&toast, &TEST_THEME, NOW_MS + TOAST_DURATION_MS);
    CHECK(toast.visible);
    toast_update(&toast, &TEST_THEME, NOW_MS + TOAST_LONG_DURATION_MS);
    CHECK(!toast.visible);
}

static void a_toast_has_at_most_seven_lines_and_stays_on_screen(void)
{
    Toast toast = {0};
    toast_show(&toast,
               "AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB "
               "AB AB AB AB AB AB AB AB AB",
               NOW_MS);
    CHECK_EQUAL(TOAST_LINES, toast_line_count(&toast, &TEST_THEME));
    Rect area = toast_area(&toast, &TEST_THEME);
    CHECK(area.y >= 0);
    CHECK(area.x >= 0);
    CHECK(area.x + area.width <= TEST_SCREEN_WIDTH);
    CHECK(area.y + area.height <= TEST_SCREEN_HEIGHT);
}

static void a_short_toast_keeps_one_line(void)
{
    Toast toast = {0};
    toast_show(&toast, "AB AB", NOW_MS);
    CHECK_EQUAL(1, toast_line_count(&toast, &TEST_THEME));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_new_toast_is_hidden),
        TEST_CASE(a_shown_toast_lasts_its_duration),
        TEST_CASE(a_visible_toast_paints_its_box),
        TEST_CASE(a_hidden_toast_paints_nothing),
        TEST_CASE(a_long_toast_wraps_onto_more_lines_and_stays_longer),
        TEST_CASE(a_toast_has_at_most_seven_lines_and_stays_on_screen),
        TEST_CASE(a_short_toast_keeps_one_line),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
