#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "app_internal.h"
#include "check.h"
#include "core/key.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "library/cover_state.h"
#include "settings/settings.h"

enum {
    START_MS = 1000,
    KEY_HOLD_MS = 30,
    MUSIC_ALBUMS_INDEX = 3,
    HOME_SETTINGS_INDEX = 1,
    SETTINGS_SCAN_COVERS_INDEX = 15,
    SETTINGS_BACKGROUND_COVERS_INDEX = 16,
    SYNC_ICON_X = 29,
    SYNC_ICON_Y = 5,
    SYNC_ICON_SIZE = 13,
    SCREEN_TIMEOUT_MS = 30000,
    QUIET_MS = 20000,
    PAUSED_AT_MS = 42000,
    KEY_AFTER_READS = 2
};

static uint32_t now_ms;

static void step(void)
{
    fake_hal_set_milliseconds(now_ms);
    app_step();
}

static void wait_ms(uint32_t duration_ms)
{
    now_ms += duration_ms;
    step();
}

static void tap(Key key)
{
    fake_hal_queue_transition(key, true, now_ms);
    fake_hal_queue_transition(key, false, now_ms + KEY_HOLD_MS);
    now_ms += KEY_HOLD_MS + 10;
    step();
}

static void release(Key key)
{
    fake_hal_queue_transition(key, false, now_ms + KEY_HOLD_MS);
    now_ms += KEY_HOLD_MS + 10;
    step();
}

static void set_battery(uint8_t percent, bool usb_power)
{
    fake_hal_set_battery((HalBattery){
        .known = true, .percent = percent, .millivolts = 3900, .usb_power = usb_power});
}

static void add_library_files(void)
{
    uint32_t size = 0;
    uint32_t covers_size = 0;
    const uint8_t *bytes = fixture_bytes(&size);
    const uint8_t *covers = fixture_covers_bytes(&covers_size);
    fake_hal_reset();
    fake_hal_add_file("/.echo/library.db", bytes, size);
    fake_hal_add_file("/.echo/covers.bin", covers, covers_size);
    fake_hal_add_file("/.echo/build.tmp", NULL, 16 * 1024 * 1024);
}

static void start_app(void)
{
    set_battery(80, false);
    now_ms = START_MS;
    fake_hal_set_milliseconds(now_ms);
    app_start();
}

static void start_with_library(void)
{
    add_library_files();
    start_app();
}

static void start_with_pending(uint32_t pending)
{
    static uint8_t state[COVER_STATE_FILE_BYTES];
    cover_state_reset(state);
    cover_state_open(state, 1);
    cover_state_set_pending(state, pending);
    cover_state_seal(state);
    add_library_files();
    fake_hal_add_file("/.echo/covers_state.bin", state, sizeof state);
    start_app();
}

static void play_first_album_track(void)
{
    tap(KEY_PLAY);
    for (int row = 0; row < MUSIC_ALBUMS_INDEX; row++) {
        tap(KEY_VOLUME_UP);
    }
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    tap(KEY_PLAY);
}

static void pause_in_the_dark_after(void (*start)(void))
{
    start();
    play_first_album_track();
    fake_hal_audio_set_position(PAUSED_AT_MS);
    tap(KEY_PLAY);
    CHECK_EQUAL(PLAYER_PAUSED, app_player_status().state);
    tap(KEY_POWER);
    CHECK(!fake_hal_display_on());
}

static void pause_in_the_dark(void)
{
    pause_in_the_dark_after(start_with_library);
}

static void a_paused_dark_player_fills_covers_after_twenty_seconds(void)
{
    pause_in_the_dark();
    int before = fake_hal_scratch_borrow_count();
    wait_ms(QUIET_MS - 1000);
    CHECK_EQUAL(before, fake_hal_scratch_borrow_count());
    wait_ms(1000);
    CHECK_EQUAL(before + 1, fake_hal_scratch_borrow_count());
    CHECK_EQUAL(0, fake_hal_scratch_outstanding());
    CHECK_EQUAL(0, fake_hal_borrows_while_playing());
}

static void the_screen_going_dark_on_its_own_after_a_long_pause_starts_at_once(void)
{
    start_with_library();
    play_first_album_track();
    tap(KEY_PLAY);
    int before = fake_hal_scratch_borrow_count();
    wait_ms(SCREEN_TIMEOUT_MS);
    CHECK(!fake_hal_display_on());
    CHECK_EQUAL(before + 1, fake_hal_scratch_borrow_count());
}

static void playing_music_never_fills_covers(void)
{
    start_with_library();
    play_first_album_track();
    int before = fake_hal_scratch_borrow_count();
    for (int minute = 0; minute < 5; minute++) {
        wait_ms(60000);
    }
    CHECK(!fake_hal_display_on());
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    CHECK_EQUAL(before, fake_hal_scratch_borrow_count());
    CHECK_EQUAL(0, fake_hal_borrows_while_playing());
}

static void a_full_buffer_lets_covers_fill_while_playing_without_the_paused_memory(void)
{
    start_with_library();
    play_first_album_track();
    fake_hal_audio_set_format((HalAudioFormat){.sample_rate = 44100});
    fake_hal_set_audio_stats((HalAudioStats){.buffered_ms = 1000});
    int scratch = fake_hal_scratch_borrow_count();
    wait_ms(SCREEN_TIMEOUT_MS);
    CHECK(!fake_hal_display_on());
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    CHECK(fake_hal_play_borrow_count() > 0);
    CHECK_EQUAL(scratch, fake_hal_scratch_borrow_count());
}

static void an_empty_buffer_keeps_covers_from_filling_while_playing(void)
{
    start_with_library();
    play_first_album_track();
    fake_hal_audio_set_format((HalAudioFormat){.sample_rate = 44100});
    wait_ms(SCREEN_TIMEOUT_MS);
    CHECK_EQUAL(0, fake_hal_play_borrow_count());
}

static void a_lit_screen_waits(void)
{
    start_with_library();
    play_first_album_track();
    tap(KEY_PLAY);
    int before = fake_hal_scratch_borrow_count();
    for (int second = 0; second < 25; second++) {
        tap(KEY_VOLUME_UP);
        wait_ms(1000);
    }
    CHECK(fake_hal_display_on());
    CHECK_EQUAL(before, fake_hal_scratch_borrow_count());
}

static void usb_power_waits(void)
{
    pause_in_the_dark();
    set_battery(80, true);
    int before = fake_hal_scratch_borrow_count();
    wait_ms(QUIET_MS);
    CHECK_EQUAL(before, fake_hal_scratch_borrow_count());
}

static void a_low_battery_waits(void)
{
    pause_in_the_dark();
    set_battery(19, false);
    int before = fake_hal_scratch_borrow_count();
    wait_ms(QUIET_MS);
    CHECK_EQUAL(before, fake_hal_scratch_borrow_count());
}

static void a_pending_restart_waits(void)
{
    pause_in_the_dark();
    fake_hal_set_restart_pending(true);
    int before = fake_hal_scratch_borrow_count();
    wait_ms(QUIET_MS);
    CHECK_EQUAL(before, fake_hal_scratch_borrow_count());
}

static int reads_of_a_whole_session(void)
{
    pause_in_the_dark();
    int before = fake_hal_read_count();
    wait_ms(QUIET_MS);
    return fake_hal_read_count() - before;
}

static int reads_of_a_session_cut_by(Key key)
{
    pause_in_the_dark();
    int before = fake_hal_read_count();
    int borrows = fake_hal_scratch_borrow_count();
    fake_hal_press_after_reads(key, KEY_AFTER_READS);
    wait_ms(QUIET_MS);
    CHECK_EQUAL(borrows + 1, fake_hal_scratch_borrow_count());
    CHECK_EQUAL(0, fake_hal_scratch_outstanding());
    return fake_hal_read_count() - before;
}

static void every_key_cuts_a_session_short(void)
{
    static const Key KEYS[] = {KEY_PLAY, KEY_VOLUME_UP, KEY_VOLUME_DOWN, KEY_BACK, KEY_POWER};
    int whole = reads_of_a_whole_session();
    for (size_t index = 0; index < sizeof KEYS / sizeof KEYS[0]; index++) {
        int cut = reads_of_a_session_cut_by(KEYS[index]);
        printf("key %d: %d reads after the press, %d in a whole session\n", (int)KEYS[index],
               cut - KEY_AFTER_READS, whole);
        CHECK(cut < whole);
        release(KEYS[index]);
        CHECK_EQUAL(0, fake_hal_borrows_while_playing());
    }
}

static void play_during_a_session_resumes_where_it_paused(void)
{
    int played = 0;
    reads_of_a_session_cut_by(KEY_PLAY);
    played = fake_hal_audio_play_count();
    release(KEY_PLAY);
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    CHECK_EQUAL(played + 1, fake_hal_audio_play_count());
    CHECK_EQUAL(PAUSED_AT_MS, fake_hal_audio_play_start());
}

static void a_cut_session_waits_for_quiet_again(void)
{
    reads_of_a_session_cut_by(KEY_VOLUME_UP);
    release(KEY_VOLUME_UP);
    int borrows = fake_hal_scratch_borrow_count();
    wait_ms(QUIET_MS / 2);
    CHECK_EQUAL(borrows, fake_hal_scratch_borrow_count());
    wait_ms(QUIET_MS);
    CHECK_EQUAL(borrows + 1, fake_hal_scratch_borrow_count());
}

static void a_finished_fill_stops_asking_for_the_scratch(void)
{
    pause_in_the_dark();
    wait_ms(QUIET_MS);
    int borrows = fake_hal_scratch_borrow_count();
    for (int minute = 0; minute < 3; minute++) {
        wait_ms(60000);
    }
    CHECK_EQUAL(borrows, fake_hal_scratch_borrow_count());
}

static bool toast_is(const char *expected)
{
    return app_toast_text() != NULL && strcmp(expected, app_toast_text()) == 0;
}

static void ask_to_scan_all_covers(void)
{
    for (int row = 0; row < HOME_SETTINGS_INDEX; row++) {
        tap(KEY_VOLUME_UP);
    }
    tap(KEY_PLAY);
    for (int row = 0; row < SETTINGS_SCAN_COVERS_INDEX; row++) {
        tap(KEY_VOLUME_UP);
    }
    tap(KEY_PLAY);
}

static void scanning_all_covers_runs_in_the_foreground_and_says_how_many(void)
{
    start_with_library();
    int before = fake_hal_scratch_borrow_count();
    ask_to_scan_all_covers();
    step();
    CHECK_EQUAL(before + 1, fake_hal_scratch_borrow_count());
    CHECK_EQUAL(0, fake_hal_scratch_outstanding());
    CHECK(toast_is("Covers: 0 new"));
}

static void back_stops_the_scan_of_all_covers(void)
{
    start_with_library();
    ask_to_scan_all_covers();
    fake_hal_press_after_reads(KEY_BACK, KEY_AFTER_READS);
    step();
    CHECK(toast_is("Cover scan stopped"));
    CHECK_EQUAL(0, fake_hal_scratch_outstanding());
}

static void scanning_all_covers_pauses_the_music_first(void)
{
    start_with_library();
    play_first_album_track();
    while (app_screen_depth() > 1) {
        tap(KEY_BACK);
    }
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    ask_to_scan_all_covers();
    step();
    CHECK(toast_is("Covers: 0 new"));
    CHECK_EQUAL(PLAYER_PAUSED, app_player_status().state);
    CHECK_EQUAL(0, fake_hal_borrows_while_playing());
}

static void start_with_nothing_pending(void)
{
    start_with_pending(0);
}

static void start_with_three_pending(void)
{
    start_with_pending(3);
}

static void nothing_pending_means_no_sync_and_no_session(void)
{
    pause_in_the_dark_after(start_with_nothing_pending);
    CHECK(!app_covers_syncing());
    int before = fake_hal_scratch_borrow_count();
    wait_ms(QUIET_MS);
    CHECK_EQUAL(before, fake_hal_scratch_borrow_count());
    CHECK(!app_covers_syncing());
}

static void pending_covers_sync_until_a_session_finishes_them(void)
{
    pause_in_the_dark_after(start_with_three_pending);
    CHECK(app_covers_syncing());
    int before = fake_hal_scratch_borrow_count();
    wait_ms(QUIET_MS);
    CHECK_EQUAL(before + 1, fake_hal_scratch_borrow_count());
    CHECK(!app_covers_syncing());
}

static void an_unknown_count_syncs_until_the_first_session(void)
{
    pause_in_the_dark();
    CHECK(app_covers_syncing());
    wait_ms(QUIET_MS);
    CHECK(!app_covers_syncing());
}

static void a_damaged_state_file_reads_as_unknown(void)
{
    static uint8_t state[COVER_STATE_FILE_BYTES];
    cover_state_reset(state);
    cover_state_set_pending(state, 0);
    cover_state_seal(state);
    state[COVER_STATE_BYTES - 1] ^= 1;
    add_library_files();
    fake_hal_add_file("/.echo/covers_state.bin", state, sizeof state);
    start_app();
    CHECK(app_covers_syncing());
}

static void choose_background_covers(int choice)
{
    for (int row = 0; row < HOME_SETTINGS_INDEX; row++) {
        tap(KEY_VOLUME_UP);
    }
    tap(KEY_PLAY);
    for (int row = 0; row < SETTINGS_BACKGROUND_COVERS_INDEX; row++) {
        tap(KEY_VOLUME_UP);
    }
    tap(KEY_PLAY);
    for (int row = 0; row < choice; row++) {
        tap(KEY_VOLUME_UP);
    }
    tap(KEY_PLAY);
    while (app_screen_depth() > 1) {
        tap(KEY_BACK);
    }
}

static int borrows_after_a_dark_pause(void)
{
    play_first_album_track();
    fake_hal_audio_set_position(PAUSED_AT_MS);
    tap(KEY_PLAY);
    tap(KEY_POWER);
    int before = fake_hal_scratch_borrow_count();
    wait_ms(QUIET_MS);
    return fake_hal_scratch_borrow_count() - before;
}

static void background_covers_off_stops_idle_fills_and_hides_the_sync(void)
{
    start_with_pending(3);
    CHECK(app_covers_syncing());
    choose_background_covers(BACKGROUND_COVERS_OFF);
    CHECK(!app_covers_syncing());
    CHECK_EQUAL(0, borrows_after_a_dark_pause());
    CHECK(!app_covers_syncing());
}

static void background_covers_off_is_kept_after_a_restart(void)
{
    start_with_pending(3);
    choose_background_covers(BACKGROUND_COVERS_OFF);
    start_app();
    CHECK(!app_covers_syncing());
    CHECK_EQUAL(0, borrows_after_a_dark_pause());
}

static void background_covers_when_paused_fills_as_before(void)
{
    start_with_pending(3);
    choose_background_covers(BACKGROUND_COVERS_PAUSED);
    CHECK(app_covers_syncing());
    CHECK_EQUAL(1, borrows_after_a_dark_pause());
    CHECK(!app_covers_syncing());
}

static void scanning_all_covers_still_works_with_background_covers_off(void)
{
    start_with_pending(3);
    choose_background_covers(BACKGROUND_COVERS_OFF);
    start_app();
    int before = fake_hal_scratch_borrow_count();
    ask_to_scan_all_covers();
    step();
    CHECK_EQUAL(before + 1, fake_hal_scratch_borrow_count());
    CHECK(toast_is("Covers: 0 new"));
}

static uint32_t sync_icon_pixels(void)
{
    uint32_t sum = 0;
    for (int y = SYNC_ICON_Y; y < SYNC_ICON_Y + SYNC_ICON_SIZE; y++) {
        for (int x = SYNC_ICON_X; x < SYNC_ICON_X + SYNC_ICON_SIZE; x++) {
            sum = sum * 31u + fake_hal_pixel(x, y);
        }
    }
    return sum;
}

static uint32_t sync_area_after_start(uint32_t pending)
{
    start_with_pending(pending);
    step();
    return sync_icon_pixels();
}

static void the_status_bar_shows_the_sync_icon_only_while_covers_wait(void)
{
    uint32_t idle = sync_area_after_start(0);
    uint32_t syncing = sync_area_after_start(3);
    CHECK(syncing != idle);
    choose_background_covers(BACKGROUND_COVERS_OFF);
    step();
    CHECK_EQUAL(idle, sync_icon_pixels());
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_paused_dark_player_fills_covers_after_twenty_seconds),
        TEST_CASE(nothing_pending_means_no_sync_and_no_session),
        TEST_CASE(pending_covers_sync_until_a_session_finishes_them),
        TEST_CASE(an_unknown_count_syncs_until_the_first_session),
        TEST_CASE(a_damaged_state_file_reads_as_unknown),
        TEST_CASE(background_covers_off_stops_idle_fills_and_hides_the_sync),
        TEST_CASE(background_covers_off_is_kept_after_a_restart),
        TEST_CASE(background_covers_when_paused_fills_as_before),
        TEST_CASE(scanning_all_covers_still_works_with_background_covers_off),
        TEST_CASE(the_status_bar_shows_the_sync_icon_only_while_covers_wait),
        TEST_CASE(a_full_buffer_lets_covers_fill_while_playing_without_the_paused_memory),
        TEST_CASE(an_empty_buffer_keeps_covers_from_filling_while_playing),
        TEST_CASE(playing_music_never_fills_covers),
        TEST_CASE(the_screen_going_dark_on_its_own_after_a_long_pause_starts_at_once),
        TEST_CASE(a_lit_screen_waits),
        TEST_CASE(usb_power_waits),
        TEST_CASE(a_low_battery_waits),
        TEST_CASE(a_pending_restart_waits),
        TEST_CASE(every_key_cuts_a_session_short),
        TEST_CASE(play_during_a_session_resumes_where_it_paused),
        TEST_CASE(a_cut_session_waits_for_quiet_again),
        TEST_CASE(a_finished_fill_stops_asking_for_the_scratch),
        TEST_CASE(scanning_all_covers_runs_in_the_foreground_and_says_how_many),
        TEST_CASE(back_stops_the_scan_of_all_covers),
        TEST_CASE(scanning_all_covers_pauses_the_music_first),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
