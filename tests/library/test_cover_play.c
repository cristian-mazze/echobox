#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "cover_card.h"
#include "library/cover_play.h"
#include "library/cover_state.h"
#include "library/covers.h"

static const Color RED = {255, 0, 0, 0xF800};
static const Color GREEN = {0, 255, 0, 0x07E0};
static const Color BLUE = {0, 0, 255, 0x001F};
static const Color WHITE = {255, 255, 255, 0xFFFF};

static const char *const TITLES[] = {"Alpha", "Beta", "Gamma", "Delta"};

enum {
    ALBUMS = 4,
    WRITE_LIMIT = 4096
};

typedef struct Pace {
    uint32_t now;
    uint32_t services;
    uint32_t refuse_at;
    uint32_t permits;
    uint32_t deny_at;
    uint32_t writes;
    uint32_t longest_write;
    ByteSink inner;
} Pace;

static Pace pace;
static uint8_t state[COVER_STATE_BYTES];

static uint32_t pace_now(void *context)
{
    (void)context;
    return pace.now++;
}

static bool pace_service(void *context)
{
    (void)context;
    pace.services++;
    return pace.refuse_at == 0 || pace.services < pace.refuse_at;
}

static bool pace_permit(void *context)
{
    (void)context;
    pace.permits++;
    return pace.deny_at == 0 || pace.permits < pace.deny_at;
}

static bool counting_write(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    (void)context;
    pace.writes++;
    pace.longest_write = length > pace.longest_write ? length : pace.longest_write;
    return pace.inner.write(pace.inner.context, offset, bytes, length);
}

static void four_albums(void)
{
    card_reset();
    card_add_album("Alpha", RED);
    card_add_album("Beta", GREEN);
    card_add_album("Gamma", BLUE);
    card_add_album("Delta", WHITE);
    cover_state_reset(state);
    cover_state_open(state, 1);
    card_scan();
    pace = (Pace){0};
}

static uint32_t covered(void)
{
    Covers covers;
    uint32_t count = 0;
    if (!covers_open(&covers, (ByteSource){card_buffer_read, &card_covers}, card_covers_capacity)) {
        return 0;
    }
    for (uint32_t index = 0; index < ALBUMS; index++) {
        count += card_cover_pixel(TITLES[index]) != 0 ? 1u : 0u;
    }
    return count;
}

static ScanResult play_fill(const CoverPlay *play, CoverStats *stats)
{
    CoverPlan plan = {.budget = {0, 0}, .state = state, .play = play};
    CoverJob job = card_cover_job(true);
    pace.inner = job.sink;
    job.sink = (ByteSink){counting_write, NULL};
    return cover_fill(&job, &plan, stats);
}

static CoverPlay make_play(CoverYield *yield, bool jpeg_only)
{
    CoverPlay play = cover_play_make(yield, pace_permit, NULL);
    play.jpeg_only = jpeg_only;
    return play;
}

static CoverYield make_yield(void)
{
    CoverYield yield = cover_yield_make(pace_now, pace_service, NULL);
    yield.slice_ms = 1;
    return yield;
}

static void level_one_covers(uint8_t *copy)
{
    CoverStats stats;
    four_albums();
    CoverJob job = card_cover_job(true);
    CHECK_EQUAL(SCAN_OK,
                cover_reconcile(&job, &(CoverPlan){.budget = {0, 0}, .state = state}, &stats));
    CHECK_EQUAL(ALBUMS, stats.decoded);
    memcpy(copy, card_covers.bytes, card_covers_capacity);
}

static void playing_fills_the_same_covers_as_the_paused_pass(void)
{
    CoverStats stats;
    uint8_t *reference = malloc(card_covers_capacity);
    level_one_covers(reference);
    four_albums();
    CoverYield yield = make_yield();
    CoverPlay play = make_play(&yield, false);
    CHECK_EQUAL(SCAN_OK, play_fill(&play, &stats));
    CHECK_EQUAL(ALBUMS, stats.decoded);
    CHECK_EQUAL(0u, stats.deferred);
    CHECK_EQUAL(ALBUMS, covered());
    CHECK(memcmp(reference, card_covers.bytes, card_covers_capacity) == 0);
    CHECK(pace.services > 0u);
    free(reference);
}

static void card_writes_come_in_chunks_of_4_kb_or_less(void)
{
    CoverStats stats;
    four_albums();
    CoverYield yield = make_yield();
    CoverPlay play = make_play(&yield, false);
    CHECK_EQUAL(SCAN_OK, play_fill(&play, &stats));
    CHECK(pace.writes > 0u);
    CHECK(pace.longest_write <= WRITE_LIMIT);
    CHECK(pace.permits >= ALBUMS);
}

static void a_cover_that_needs_the_paused_pass_is_deferred_and_the_cursor_stays(void)
{
    CoverStats stats;
    four_albums();
    CoverYield yield = make_yield();
    CoverPlay play = make_play(&yield, true);
    CHECK_EQUAL(SCAN_CANCELLED, play_fill(&play, &stats));
    CHECK_EQUAL(1u, stats.deferred);
    CHECK_EQUAL(0u, covered());
    CHECK_EQUAL(0u, cover_state_cursor(state));
    CHECK_EQUAL(0u, cover_state_failure_count(state));
}

static void a_refusal_from_the_audio_service_leaves_no_cover_and_no_failure(void)
{
    CoverStats stats;
    four_albums();
    pace.refuse_at = 40;
    CoverYield yield = make_yield();
    CoverPlay play = make_play(&yield, false);
    CHECK_EQUAL(SCAN_CANCELLED, play_fill(&play, &stats));
    CHECK_EQUAL(0u, covered());
    CHECK_EQUAL(0u, stats.decoded);
    CHECK_EQUAL(0u, cover_state_failure_count(state));
    CHECK_EQUAL(0u, cover_state_cursor(state));
}

static void a_denied_write_cancels_the_session_and_a_later_pass_finishes(void)
{
    CoverStats stats;
    four_albums();
    pace.deny_at = 2;
    CoverYield yield = make_yield();
    CoverPlay play = make_play(&yield, false);
    CHECK_EQUAL(SCAN_CANCELLED, play_fill(&play, &stats));
    CHECK(covered() < ALBUMS);
    CHECK_EQUAL(0u, cover_state_failure_count(state));
    pace.deny_at = 0;
    CoverYield again = make_yield();
    CoverPlay later = make_play(&again, false);
    CHECK_EQUAL(SCAN_OK, play_fill(&later, &stats));
    CHECK_EQUAL(ALBUMS, covered());
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(playing_fills_the_same_covers_as_the_paused_pass),
        TEST_CASE(card_writes_come_in_chunks_of_4_kb_or_less),
        TEST_CASE(a_cover_that_needs_the_paused_pass_is_deferred_and_the_cursor_stays),
        TEST_CASE(a_refusal_from_the_audio_service_leaves_no_cover_and_no_failure),
        TEST_CASE(a_denied_write_cancels_the_session_and_a_later_pass_finishes),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
