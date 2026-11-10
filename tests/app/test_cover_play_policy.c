#include "check.h"
#include "cover_play_policy.h"

static CoverPlayInput playing(uint32_t buffered_ms)
{
    return (CoverPlayInput){
        .playing = true, .sample_rate = 44100, .buffered_ms = buffered_ms, .underruns = 0};
}

static CoverPlayGuard settled(uint32_t full_ms)
{
    CoverPlayGuard guard;
    cover_play_guard_init(&guard);
    CoverPlayInput input = playing(full_ms);
    cover_play_observe(&guard, &input);
    return guard;
}

static void nothing_starts_before_a_full_level_has_been_seen(void)
{
    CoverPlayGuard guard;
    cover_play_guard_init(&guard);
    CoverPlayInput input = playing(300);
    CHECK(!cover_play_may_start(&guard, &input));
}

static void a_session_starts_at_85_percent_of_the_full_level(void)
{
    CoverPlayGuard guard = settled(300);
    CoverPlayInput below = playing(254);
    CoverPlayInput at = playing(255);
    CHECK(!cover_play_may_start(&guard, &below));
    CHECK(cover_play_may_start(&guard, &at));
    CHECK(guard.session_open);
}

static void rates_above_48_khz_never_start(void)
{
    CoverPlayGuard guard = settled(300);
    CoverPlayInput input = playing(300);
    input.sample_rate = 96000;
    CHECK(!cover_play_may_start(&guard, &input));
    input.sample_rate = 48000;
    CHECK(cover_play_may_start(&guard, &input));
}

static void paused_music_never_starts_a_playing_session(void)
{
    CoverPlayGuard guard = settled(300);
    CoverPlayInput input = playing(300);
    input.playing = false;
    CHECK(!cover_play_may_start(&guard, &input));
}

static void the_session_is_cancelled_below_half_the_full_level(void)
{
    CoverPlayGuard guard = settled(300);
    CoverPlayInput input = playing(300);
    CHECK(cover_play_may_start(&guard, &input));
    input.buffered_ms = 150;
    CHECK(cover_play_may_continue(&guard, &input));
    input.buffered_ms = 149;
    CHECK(!cover_play_may_continue(&guard, &input));
    CHECK(!guard.disabled);
}

static void writes_wait_for_the_high_mark(void)
{
    CoverPlayGuard guard = settled(300);
    CoverPlayInput input = playing(300);
    CHECK(cover_play_may_start(&guard, &input));
    input.buffered_ms = 200;
    CHECK(cover_play_may_continue(&guard, &input));
    CHECK(!cover_play_may_write(&guard, &input));
    input.buffered_ms = 260;
    CHECK(cover_play_may_write(&guard, &input));
}

static void an_underrun_switches_level_two_off_until_the_next_boot(void)
{
    CoverPlayGuard guard = settled(300);
    CoverPlayInput input = playing(300);
    CHECK(cover_play_may_start(&guard, &input));
    input.underruns = 1;
    CHECK(!cover_play_may_continue(&guard, &input));
    CHECK(guard.disabled);
    cover_play_end(&guard);
    CHECK(!cover_play_may_start(&guard, &input));
}

static void a_gap_over_ten_milliseconds_switches_it_off(void)
{
    CoverPlayGuard guard = settled(300);
    cover_play_note_gap(&guard, 10);
    CHECK(!guard.disabled);
    cover_play_note_gap(&guard, 11);
    CHECK(guard.disabled);
}

static void a_new_track_forgets_the_full_level(void)
{
    CoverPlayGuard guard = settled(300);
    cover_play_track_changed(&guard);
    CoverPlayInput input = playing(150);
    cover_play_observe(&guard, &input);
    CHECK_EQUAL(150, guard.full_ms);
    CHECK(cover_play_may_start(&guard, &input));
}

static void the_full_level_is_frozen_while_a_session_runs(void)
{
    CoverPlayGuard guard = settled(300);
    CoverPlayInput input = playing(300);
    CHECK(cover_play_may_start(&guard, &input));
    input.buffered_ms = 500;
    cover_play_observe(&guard, &input);
    CHECK_EQUAL(300, guard.full_ms);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(nothing_starts_before_a_full_level_has_been_seen),
        TEST_CASE(a_session_starts_at_85_percent_of_the_full_level),
        TEST_CASE(rates_above_48_khz_never_start),
        TEST_CASE(paused_music_never_starts_a_playing_session),
        TEST_CASE(the_session_is_cancelled_below_half_the_full_level),
        TEST_CASE(writes_wait_for_the_high_mark),
        TEST_CASE(an_underrun_switches_level_two_off_until_the_next_boot),
        TEST_CASE(a_gap_over_ten_milliseconds_switches_it_off),
        TEST_CASE(a_new_track_forgets_the_full_level),
        TEST_CASE(the_full_level_is_frozen_while_a_session_runs),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
