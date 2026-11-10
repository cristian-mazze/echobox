#include <string.h>

#include "check.h"
#include "fonts/fonts.h"
#include "ui/header.h"
#include "ui/status_bar.h"
#include "ui_test_canvas.h"
#include "ui_test_theme.h"

enum {
    STATUS_HEADER_HEIGHT = 12,
    STATUS_ICON_SIZE = 6,
    LOW_BATTERY = 10,
    HALF_BATTERY = 50,
    DEVICE_LONG_SIDE = 320,
    DEVICE_SHORT_SIDE = 170,
    DEVICE_HEADER_HEIGHT = 24,
    DEVICE_PORTRAIT_STATUS = 16,
    DEVICE_PADDING = 6,
    DEVICE_ICON_SIZE = 10
};

static Theme status_theme(void)
{
    Theme theme = TEST_THEME;
    theme.metrics.header_height = STATUS_HEADER_HEIGHT;
    theme.metrics.status_icon_size = STATUS_ICON_SIZE;
    return theme;
}

static Canvas draw_status(const Theme *theme, StatusBarState state)
{
    Canvas canvas = ui_test_canvas();
    header_draw(&canvas, theme, "");
    status_bar_draw(&canvas, theme, state);
    return canvas;
}

static StatusBarState battery_at(uint8_t percent, bool charging)
{
    return (StatusBarState){.playback = STATUS_PLAYBACK_NONE,
                            .battery_known = true,
                            .show_percent = true,
                            .battery_percent = percent,
                            .charging = charging};
}

static void an_idle_player_without_a_battery_reading_draws_nothing(void)
{
    Theme theme = status_theme();
    Canvas canvas = draw_status(&theme, (StatusBarState){.playback = STATUS_PLAYBACK_NONE});
    Rect header = {0, 0, TEST_SCREEN_WIDTH, STATUS_HEADER_HEIGHT - 1};
    CHECK(ui_test_area_has_only(&canvas, header, theme.colors.header_background));
}

static void a_playing_player_shows_the_play_icon_at_the_left(void)
{
    Theme theme = status_theme();
    Canvas canvas = draw_status(&theme, (StatusBarState){.playback = STATUS_PLAYBACK_PLAYING});
    Rect icon = status_bar_playback_area(&theme);
    Rect middle = {icon.x + 2, icon.y, 3, icon.height};
    CHECK(icon.x + icon.width <= TEST_HEADER_INSET);
    CHECK(ui_test_area_has_color(&canvas, middle, theme.colors.header_text));
}

static void a_paused_player_shows_two_bars(void)
{
    Theme theme = status_theme();
    Canvas canvas = draw_status(&theme, (StatusBarState){.playback = STATUS_PLAYBACK_PAUSED});
    Rect icon = status_bar_playback_area(&theme);
    Rect first = {icon.x, icon.y, 1, icon.height};
    Rect gap = {icon.x + 2, icon.y, 3, icon.height};
    Rect last = {icon.x + icon.width - 1, icon.y, 1, icon.height};
    CHECK(ui_test_area_has_only(&canvas, first, theme.colors.header_text));
    CHECK(!ui_test_area_has_color(&canvas, gap, theme.colors.header_text));
    CHECK(ui_test_area_has_only(&canvas, last, theme.colors.header_text));
}

static void the_battery_sits_inside_the_right_inset(void)
{
    Theme theme = status_theme();
    Canvas canvas = draw_status(&theme, battery_at(HALF_BATTERY, false));
    Rect battery = status_bar_battery_area(&theme);
    CHECK(battery.x >= TEST_SCREEN_WIDTH - TEST_HEADER_INSET);
    CHECK(battery.x + battery.width <= TEST_SCREEN_WIDTH);
    CHECK(ui_test_area_has_color(&canvas, battery, theme.colors.header_text));
}

static void the_fill_follows_the_charge(void)
{
    CHECK_EQUAL(0, status_bar_battery_fill_width(0, 16));
    CHECK_EQUAL(1, status_bar_battery_fill_width(1, 16));
    CHECK_EQUAL(8, status_bar_battery_fill_width(HALF_BATTERY, 16));
    CHECK_EQUAL(16, status_bar_battery_fill_width(100, 16));
    CHECK_EQUAL(16, status_bar_battery_fill_width(250, 16));
}

static void a_low_battery_is_drawn_in_the_danger_color(void)
{
    Theme theme = status_theme();
    Canvas canvas = draw_status(&theme, battery_at(LOW_BATTERY, false));
    CHECK(ui_test_area_has_color(&canvas, status_bar_battery_area(&theme), theme.colors.danger));
}

static void a_charging_battery_is_drawn_in_the_success_color(void)
{
    Theme theme = status_theme();
    Canvas canvas = draw_status(&theme, battery_at(LOW_BATTERY, true));
    Rect battery = status_bar_battery_area(&theme);
    CHECK(ui_test_area_has_color(&canvas, battery, theme.colors.success));
    CHECK(!ui_test_area_has_color(&canvas, battery, theme.colors.danger));
}

static void the_percentage_reads_as_a_number_and_a_percent_sign(void)
{
    char text[STATUS_BAR_PERCENT_CAPACITY];
    status_bar_percent_text(73, text, sizeof text);
    CHECK(strcmp("73%", text) == 0);
    status_bar_percent_text(100, text, sizeof text);
    CHECK(strcmp("100%", text) == 0);
    status_bar_percent_text(0, text, sizeof text);
    CHECK(strcmp("0%", text) == 0);
}

static void the_percentage_sits_just_left_of_the_battery(void)
{
    Theme theme = status_theme();
    Rect battery = status_bar_battery_area(&theme);
    Rect percent = status_bar_percent_area(&theme, "73%", false);
    CHECK(percent.width > 0);
    CHECK(percent.x + percent.width < battery.x);
    CHECK(percent.x + percent.width >= battery.x - theme.metrics.padding);
}

static void a_known_battery_shows_its_percentage(void)
{
    Theme theme = status_theme();
    Canvas canvas = draw_status(&theme, battery_at(HALF_BATTERY, false));
    Rect percent = status_bar_percent_area(&theme, "50%", false);
    CHECK(!ui_test_area_has_only(&canvas, percent, theme.colors.header_background));
}

static void a_hidden_percentage_leaves_its_spot_empty(void)
{
    Theme theme = status_theme();
    StatusBarState state = battery_at(HALF_BATTERY, false);
    state.show_percent = false;
    Canvas canvas = draw_status(&theme, state);
    Rect percent = status_bar_percent_area(&theme, "50%", false);
    CHECK(ui_test_area_has_only(&canvas, percent, theme.colors.header_background));
}

static void an_unknown_battery_shows_no_percentage(void)
{
    Theme theme = status_theme();
    StatusBarState state = battery_at(HALF_BATTERY, false);
    state.battery_known = false;
    Canvas canvas = draw_status(&theme, state);
    Rect percent = status_bar_percent_area(&theme, "50%", false);
    CHECK(ui_test_area_has_only(&canvas, percent, theme.colors.header_background));
}

static void a_charging_battery_has_a_bolt_beside_it(void)
{
    Theme theme = status_theme();
    Rect bolt = status_bar_bolt_area(&theme);
    Rect battery = status_bar_battery_area(&theme);
    CHECK(bolt.x + bolt.width < battery.x);
    Canvas charging = draw_status(&theme, battery_at(50, true));
    CHECK(ui_test_area_has_color(&charging, bolt, theme.colors.success));
    Canvas resting = draw_status(&theme, battery_at(50, false));
    CHECK(!ui_test_area_has_color(&resting, bolt, theme.colors.success));
}

static void the_percentage_moves_left_of_the_bolt_while_charging(void)
{
    Theme theme = status_theme();
    Rect bolt = status_bar_bolt_area(&theme);
    Rect percent = status_bar_percent_area(&theme, "50%", true);
    CHECK(percent.x + percent.width < bolt.x);
    Canvas canvas = draw_status(&theme, battery_at(50, true));
    CHECK(!ui_test_area_has_only(&canvas, percent, theme.colors.header_background));
}

static void a_syncing_player_shows_the_sync_icon(void)
{
    Theme theme = status_theme();
    StatusBarState state = {.playback = STATUS_PLAYBACK_NONE, .syncing = true};
    Canvas canvas = draw_status(&theme, state);
    CHECK(
        ui_test_area_has_color(&canvas, status_bar_sync_area(&theme), theme.colors.text_secondary));
}

static void without_sync_its_spot_stays_empty(void)
{
    Theme theme = status_theme();
    Canvas canvas = draw_status(&theme, (StatusBarState){.playback = STATUS_PLAYBACK_NONE});
    CHECK(ui_test_area_has_only(&canvas, status_bar_sync_area(&theme),
                                theme.colors.header_background));
}

static Theme device_theme(bool portrait)
{
    Theme theme = TEST_THEME;
    theme.fonts.small = &FONT_TERMINUS_BOLD_12;
    theme.metrics.screen_width = portrait ? DEVICE_SHORT_SIDE : DEVICE_LONG_SIDE;
    theme.metrics.screen_height = portrait ? DEVICE_LONG_SIDE : DEVICE_SHORT_SIDE;
    theme.metrics.status_height = portrait ? DEVICE_PORTRAIT_STATUS : 0;
    theme.metrics.header_height = DEVICE_HEADER_HEIGHT + (portrait ? DEVICE_PORTRAIT_STATUS : 0);
    theme.metrics.padding = DEVICE_PADDING;
    theme.metrics.status_icon_size = DEVICE_ICON_SIZE;
    return theme;
}

static bool apart(Rect first, Rect second)
{
    return first.x + first.width <= second.x || second.x + second.width <= first.x;
}

static void the_sync_icon_keeps_clear_of_its_neighbours_on_the_device(void)
{
    for (int portrait = 0; portrait < 2; portrait++) {
        Theme theme = device_theme(portrait == 1);
        Rect sync = status_bar_sync_area(&theme);
        int band = portrait == 1 ? DEVICE_PORTRAIT_STATUS : DEVICE_HEADER_HEIGHT;
        CHECK(sync.y >= 0 && sync.y + sync.height <= band);
        CHECK(apart(sync, status_bar_playback_area(&theme)));
        CHECK(apart(sync, status_bar_bluetooth_area(&theme)));
        CHECK(apart(sync, status_bar_bolt_area(&theme)));
        CHECK(apart(sync, status_bar_percent_area(&theme, "100%", true)));
        CHECK(sync.x + sync.width <= status_bar_percent_area(&theme, "100%", true).x);
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_percentage_reads_as_a_number_and_a_percent_sign),
        TEST_CASE(the_percentage_sits_just_left_of_the_battery),
        TEST_CASE(a_known_battery_shows_its_percentage),
        TEST_CASE(a_hidden_percentage_leaves_its_spot_empty),
        TEST_CASE(an_unknown_battery_shows_no_percentage),
        TEST_CASE(an_idle_player_without_a_battery_reading_draws_nothing),
        TEST_CASE(a_playing_player_shows_the_play_icon_at_the_left),
        TEST_CASE(a_paused_player_shows_two_bars),
        TEST_CASE(the_battery_sits_inside_the_right_inset),
        TEST_CASE(the_fill_follows_the_charge),
        TEST_CASE(a_low_battery_is_drawn_in_the_danger_color),
        TEST_CASE(a_charging_battery_is_drawn_in_the_success_color),
        TEST_CASE(a_charging_battery_has_a_bolt_beside_it),
        TEST_CASE(the_percentage_moves_left_of_the_bolt_while_charging),
        TEST_CASE(a_syncing_player_shows_the_sync_icon),
        TEST_CASE(without_sync_its_spot_stays_empty),
        TEST_CASE(the_sync_icon_keeps_clear_of_its_neighbours_on_the_device),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
