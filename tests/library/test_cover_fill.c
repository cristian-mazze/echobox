#include <string.h>

#include "check.h"
#include "cover_card.h"
#include "library/cover_build.h"
#include "library/cover_state.h"

static const Color RED = {255, 0, 0, 0xF800};
static const Color GREEN = {0, 255, 0, 0x07E0};
static const Color BLUE = {0, 0, 255, 0x001F};
static const Color WHITE = {255, 255, 255, 0xFFFF};
static const Color YELLOW = {255, 255, 0, 0xFFE0};

static const char *const TITLES[] = {"Alpha", "Beta", "Gamma", "Delta", "Epsilon"};

typedef struct TestClock {
    uint32_t now;
    uint32_t step;
} TestClock;

typedef struct Cancelling {
    ByteSource inner;
    bool *cancel;
} Cancelling;

static uint8_t state[COVER_STATE_BYTES];
static bool cancel_flag;
static Cancelling cancelling;
static ScanFileSystem plain_files;

static void five_albums(void)
{
    card_reset();
    card_add_album("Alpha", RED);
    card_add_album("Beta", GREEN);
    card_add_album("Gamma", BLUE);
    card_add_album("Delta", WHITE);
    card_add_album("Epsilon", YELLOW);
    cover_state_reset(state);
    cover_state_open(state, 1);
    card_scan();
}

static uint32_t covered(void)
{
    uint32_t count = 0;
    for (uint32_t index = 0; index < 5; index++) {
        count += card_cover_pixel(TITLES[index]) != 0 ? 1u : 0u;
    }
    return count;
}

static CoverPlan plan_for(uint32_t max_albums)
{
    return (CoverPlan){.budget = {0, max_albums}, .state = state, .cancel = &cancel_flag};
}

static ScanResult reconcile(const CoverPlan *plan, CoverStats *stats)
{
    CoverJob job = card_cover_job(true);
    return cover_reconcile(&job, plan, stats);
}

static ScanResult fill(const CoverPlan *plan, CoverStats *stats)
{
    CoverJob job = card_cover_job(true);
    return cover_fill(&job, plan, stats);
}

static uint32_t tick(void *context)
{
    TestClock *clock = context;
    clock->now += clock->step;
    return clock->now;
}

static void the_initial_budget_stops_after_its_album_limit(void)
{
    CoverStats stats;
    CoverPlan plan = plan_for(2);
    five_albums();
    CHECK_EQUAL(SCAN_OK, reconcile(&plan, &stats));
    CHECK_EQUAL(2u, stats.decoded);
    CHECK_EQUAL(3u, stats.pending);
    CHECK_EQUAL(2u, covered());
}

static void the_initial_budget_stops_at_its_time_limit(void)
{
    CoverStats stats;
    TestClock clock = {0, 1000};
    CoverPlan plan = plan_for(0);
    plan.budget.max_ms = 2500;
    plan.clock = (CoverClock){tick, &clock};
    five_albums();
    CHECK_EQUAL(SCAN_OK, reconcile(&plan, &stats));
    CHECK(stats.decoded > 0u);
    CHECK(stats.decoded < 5u);
    CHECK_EQUAL(5u, stats.decoded + stats.pending);
}

static void the_initial_budget_starts_with_the_resume_album(void)
{
    CoverStats stats;
    CoverPlan plan = plan_for(1);
    five_albums();
    plan.anchored = true;
    plan.anchor_key = card_album_key("Delta");
    CHECK_EQUAL(SCAN_OK, reconcile(&plan, &stats));
    CHECK_EQUAL(1u, stats.decoded);
    CHECK_EQUAL(WHITE.pixel, card_cover_pixel("Delta"));
    CHECK_EQUAL(1u, covered());
}

static void filling_continues_from_the_cursor_until_every_cover_is_done(void)
{
    CoverStats stats;
    CoverPlan plan = plan_for(1);
    five_albums();
    CHECK_EQUAL(SCAN_OK, reconcile(&plan, &stats));
    for (uint32_t round = 0; round < 4; round++) {
        CHECK_EQUAL(SCAN_OK, fill(&plan, &stats));
        CHECK_EQUAL(1u, stats.decoded);
    }
    CHECK_EQUAL(5u, covered());
    CHECK_EQUAL(SCAN_OK, fill(&plan, &stats));
    CHECK_EQUAL(0u, stats.decoded);
    CHECK_EQUAL(0u, stats.pending);
    CHECK_EQUAL(5u, cover_state_cursor(state));
}

static void filling_serves_the_priority_album_first(void)
{
    CoverStats stats;
    CoverPlan plan = plan_for(1);
    five_albums();
    plan.anchored = true;
    plan.anchor_key = card_album_key("Alpha");
    CHECK_EQUAL(SCAN_OK, reconcile(&plan, &stats));
    plan.anchored = false;
    uint16_t wanted = (uint16_t)card_album_index("Epsilon");
    plan.priority = &wanted;
    plan.priority_count = 1;
    CHECK_EQUAL(SCAN_OK, fill(&plan, &stats));
    CHECK_EQUAL(1u, stats.decoded);
    CHECK_EQUAL(YELLOW.pixel, card_cover_pixel("Epsilon"));
}

static bool cancelling_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    Cancelling *source = context;
    *source->cancel = true;
    return source->inner.read(source->inner.context, offset, buffer, length);
}

static bool cancelling_open(void *context, const char *path, ByteSource *source, uint32_t *size)
{
    if (!plain_files.open_file(context, path, &cancelling.inner, size)) {
        return false;
    }
    cancelling.cancel = &cancel_flag;
    *source = (ByteSource){cancelling_read, &cancelling};
    return true;
}

static void a_cancelled_fill_remembers_no_failure(void)
{
    CoverStats stats;
    CoverPlan plan = plan_for(0);
    five_albums();
    CHECK_EQUAL(SCAN_OK, reconcile(&(CoverPlan){.budget = {0, 1}, .state = state}, &stats));
    CoverJob job = card_cover_job(true);
    plain_files = job.files;
    job.files.open_file = cancelling_open;
    cancel_flag = false;
    CHECK_EQUAL(SCAN_CANCELLED, cover_fill(&job, &plan, &stats));
    CHECK_EQUAL(0u, stats.decoded);
    CHECK_EQUAL(0u, stats.missing);
    CHECK_EQUAL(0u, cover_state_failure_count(state));
    cancel_flag = false;
    CHECK_EQUAL(SCAN_OK, fill(&plan, &stats));
    CHECK_EQUAL(4u, stats.decoded);
    CHECK_EQUAL(5u, covered());
}

static uint32_t polls_left;

static bool stop_after_polls(void *context)
{
    (void)context;
    polls_left -= polls_left > 0 ? 1u : 0u;
    return polls_left == 0;
}

static void a_poll_that_asks_to_stop_keeps_the_cursor_on_the_unfinished_album(void)
{
    CoverStats stats;
    CoverPlan plan = plan_for(0);
    five_albums();
    CHECK_EQUAL(SCAN_OK, reconcile(&(CoverPlan){.budget = {0, 1}, .state = state}, &stats));
    plan.poll = (CoverPoll){stop_after_polls, NULL};
    polls_left = 3;
    CHECK_EQUAL(SCAN_CANCELLED, fill(&plan, &stats));
    uint32_t stopped_at = cover_state_cursor(state);
    CHECK(stopped_at < 5u);
    plan.poll = (CoverPoll){NULL, NULL};
    CHECK_EQUAL(SCAN_OK, fill(&plan, &stats));
    CHECK_EQUAL(5u, covered());
}

static void a_broken_image_is_not_retried_until_its_stamp_changes(void)
{
    CoverStats stats;
    CoverPlan plan = plan_for(0);
    card_reset();
    card_add_album("Alpha", RED);
    card_add_broken_album("Broken");
    cover_state_reset(state);
    cover_state_open(state, 1);
    card_scan();
    CHECK_EQUAL(SCAN_OK, reconcile(&plan, &stats));
    CHECK_EQUAL(1u, stats.decoded);
    CHECK_EQUAL(1u, stats.missing);
    CHECK_EQUAL(1u, cover_state_failure_count(state));
    cover_state_set_cursor(state, 0);
    card_opens = 0;
    CHECK_EQUAL(SCAN_OK, fill(&plan, &stats));
    CHECK_EQUAL(0u, card_opens);
    CHECK_EQUAL(0u, stats.decoded);
    card_replace_cover("Broken", GREEN);
    card_scan();
    CHECK_EQUAL(SCAN_OK, reconcile(&plan, &stats));
    CHECK_EQUAL(1u, stats.decoded);
    CHECK_EQUAL(GREEN.pixel, card_cover_pixel("Broken"));
    CHECK_EQUAL(0u, cover_state_failure_count(state));
}

static void albums_beyond_the_slots_are_counted_apart(void)
{
    CoverStats stats;
    CoverPlan plan = plan_for(0);
    five_albums();
    card_covers_capacity = 16 + 3 * CARD_RECORD_SLOT;
    CHECK_EQUAL(SCAN_OK, reconcile(&plan, &stats));
    CHECK_EQUAL(3u, stats.covers);
    CHECK_EQUAL(2u, stats.overflow);
    CHECK_EQUAL(0u, stats.pending);
}

static void a_damaged_state_starts_empty(void)
{
    cover_state_reset(state);
    cover_state_note_failure(state, 7, 9);
    cover_state_set_cursor(state, 3);
    cover_state_seal(state);
    CHECK(cover_state_valid(state));
    state[COVER_STATE_BYTES - 1] ^= 1;
    CHECK(!cover_state_valid(state));
    cover_state_open(state, 5);
    CHECK_EQUAL(0u, cover_state_failure_count(state));
    CHECK_EQUAL(0u, cover_state_cursor(state));
    CHECK(!cover_state_failed(state, 7, 9));
}

static void a_new_library_restarts_the_cursor_but_keeps_failures(void)
{
    static const uint32_t KEYS[] = {4, 4, 6};
    cover_state_reset(state);
    cover_state_open(state, 1);
    cover_state_set_cursor(state, 3);
    cover_state_note_failure(state, 8, 2);
    cover_state_set_queue(state, KEYS, 3);
    cover_state_seal(state);
    cover_state_open(state, 2);
    CHECK_EQUAL(0u, cover_state_cursor(state));
    CHECK(cover_state_failed(state, 8, 2));
    CHECK(!cover_state_failed(state, 8, 3));
    CHECK_EQUAL(2u, cover_state_queue_count(state));
    CHECK_EQUAL(6u, cover_state_queue_key(state, 1));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_initial_budget_stops_after_its_album_limit),
        TEST_CASE(the_initial_budget_stops_at_its_time_limit),
        TEST_CASE(the_initial_budget_starts_with_the_resume_album),
        TEST_CASE(filling_continues_from_the_cursor_until_every_cover_is_done),
        TEST_CASE(filling_serves_the_priority_album_first),
        TEST_CASE(a_cancelled_fill_remembers_no_failure),
        TEST_CASE(a_poll_that_asks_to_stop_keeps_the_cursor_on_the_unfinished_album),
        TEST_CASE(a_broken_image_is_not_retried_until_its_stamp_changes),
        TEST_CASE(albums_beyond_the_slots_are_counted_apart),
        TEST_CASE(a_damaged_state_starts_empty),
        TEST_CASE(a_new_library_restarts_the_cursor_but_keeps_failures),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
