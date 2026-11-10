#include "check.h"
#include "cover_fill_policy.h"
#include "cover_queue.h"

enum {
    NOW_MS = 100000
};

static CoverFillConditions ready_conditions(void)
{
    return (CoverFillConditions){.enabled = true,
                                 .playing = false,
                                 .quiet_since_ms = NOW_MS - COVER_FILL_QUIET_MS,
                                 .display_on = false,
                                 .usb_power = false,
                                 .battery_known = true,
                                 .battery_percent = COVER_FILL_MIN_BATTERY,
                                 .restart_pending = false,
                                 .rescan_waiting = false,
                                 .work_left = true};
}

static void a_quiet_dark_player_on_battery_is_ready(void)
{
    CoverFillConditions conditions = ready_conditions();
    CHECK(cover_fill_ready(&conditions, NOW_MS));
}

static void background_covers_off_is_never_ready(void)
{
    CoverFillConditions conditions = ready_conditions();
    conditions.enabled = false;
    CHECK(!cover_fill_ready(&conditions, NOW_MS));
}

static void playing_music_is_never_ready(void)
{
    CoverFillConditions conditions = ready_conditions();
    conditions.playing = true;
    CHECK(!cover_fill_ready(&conditions, NOW_MS));
}

static void fewer_than_twenty_quiet_seconds_are_not_ready(void)
{
    CoverFillConditions conditions = ready_conditions();
    conditions.quiet_since_ms = NOW_MS - COVER_FILL_QUIET_MS + 1;
    CHECK(!cover_fill_ready(&conditions, NOW_MS));
}

static void a_lit_screen_is_not_ready(void)
{
    CoverFillConditions conditions = ready_conditions();
    conditions.display_on = true;
    CHECK(!cover_fill_ready(&conditions, NOW_MS));
}

static void usb_power_is_not_ready(void)
{
    CoverFillConditions conditions = ready_conditions();
    conditions.usb_power = true;
    CHECK(!cover_fill_ready(&conditions, NOW_MS));
}

static void a_low_or_unknown_battery_is_not_ready(void)
{
    CoverFillConditions conditions = ready_conditions();
    conditions.battery_percent = COVER_FILL_MIN_BATTERY - 1;
    CHECK(!cover_fill_ready(&conditions, NOW_MS));
    conditions = ready_conditions();
    conditions.battery_known = false;
    CHECK(!cover_fill_ready(&conditions, NOW_MS));
}

static void a_pending_restart_or_rescan_is_not_ready(void)
{
    CoverFillConditions conditions = ready_conditions();
    conditions.restart_pending = true;
    CHECK(!cover_fill_ready(&conditions, NOW_MS));
    conditions = ready_conditions();
    conditions.rescan_waiting = true;
    CHECK(!cover_fill_ready(&conditions, NOW_MS));
}

static void nothing_left_to_fill_is_not_ready(void)
{
    CoverFillConditions conditions = ready_conditions();
    conditions.work_left = false;
    CHECK(!cover_fill_ready(&conditions, NOW_MS));
}

static void the_newest_discovery_is_served_first_after_the_current_album(void)
{
    CoverQueue queue;
    uint16_t order[COVER_QUEUE_CAPACITY];
    uint16_t current = 9;
    cover_queue_init(&queue);
    CHECK(cover_queue_push(&queue, 3, 30));
    CHECK(cover_queue_push(&queue, 5, 50));
    CHECK(cover_queue_push(&queue, 9, 90));
    CHECK(!cover_queue_push(&queue, 3, 30));
    CHECK_EQUAL(3u, cover_queue_order(&queue, &current, order, COVER_QUEUE_CAPACITY));
    CHECK_EQUAL(9, order[0]);
    CHECK_EQUAL(3, order[1]);
    CHECK_EQUAL(5, order[2]);
    CHECK_EQUAL(2u, cover_queue_order(&queue, NULL, order, 2));
    CHECK_EQUAL(3, order[0]);
    CHECK_EQUAL(30u, queue.keys[0]);
}

static void a_full_queue_forgets_its_oldest_discovery(void)
{
    CoverQueue queue;
    cover_queue_init(&queue);
    for (uint16_t album = 0; album <= COVER_QUEUE_CAPACITY; album++) {
        cover_queue_push(&queue, album, album);
    }
    CHECK_EQUAL(COVER_QUEUE_CAPACITY, queue.count);
    CHECK_EQUAL(COVER_QUEUE_CAPACITY, queue.albums[0]);
    CHECK_EQUAL(1, queue.albums[COVER_QUEUE_CAPACITY - 1]);
    cover_queue_remove(&queue, 1);
    CHECK_EQUAL(COVER_QUEUE_CAPACITY - 1, queue.count);
}

static void playing_covers_need_a_playing_dark_player_on_battery(void)
{
    CoverFillConditions conditions = ready_conditions();
    conditions.playing = true;
    CHECK(cover_fill_play_ready(&conditions));
    conditions.playing = false;
    CHECK(!cover_fill_play_ready(&conditions));
}

static void playing_covers_wait_for_the_same_things_as_paused_ones(void)
{
    CoverFillConditions conditions = ready_conditions();
    conditions.playing = true;
    conditions.display_on = true;
    CHECK(!cover_fill_play_ready(&conditions));
    conditions = ready_conditions();
    conditions.playing = true;
    conditions.usb_power = true;
    CHECK(!cover_fill_play_ready(&conditions));
    conditions = ready_conditions();
    conditions.playing = true;
    conditions.enabled = false;
    CHECK(!cover_fill_play_ready(&conditions));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_quiet_dark_player_on_battery_is_ready),
        TEST_CASE(playing_music_is_never_ready),
        TEST_CASE(background_covers_off_is_never_ready),
        TEST_CASE(fewer_than_twenty_quiet_seconds_are_not_ready),
        TEST_CASE(a_lit_screen_is_not_ready),
        TEST_CASE(usb_power_is_not_ready),
        TEST_CASE(a_low_or_unknown_battery_is_not_ready),
        TEST_CASE(a_pending_restart_or_rescan_is_not_ready),
        TEST_CASE(nothing_left_to_fill_is_not_ready),
        TEST_CASE(playing_covers_need_a_playing_dark_player_on_battery),
        TEST_CASE(playing_covers_wait_for_the_same_things_as_paused_ones),
        TEST_CASE(the_newest_discovery_is_served_first_after_the_current_album),
        TEST_CASE(a_full_queue_forgets_its_oldest_discovery),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
