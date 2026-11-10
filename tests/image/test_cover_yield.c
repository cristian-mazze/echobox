#include <stdint.h>

#include "check.h"
#include "image/cover_yield.h"

typedef struct Fake {
    uint32_t now;
    uint32_t services;
    uint32_t cancel_at;
} Fake;

static uint32_t fake_now(void *context)
{
    return ((Fake *)context)->now;
}

static bool fake_service(void *context)
{
    Fake *fake = context;
    fake->services++;
    return fake->cancel_at == 0 || fake->services < fake->cancel_at;
}

static void a_missing_hook_never_stops_the_work(void)
{
    CHECK(cover_yield_point(NULL));
}

static void the_hook_serves_only_after_a_full_slice(void)
{
    Fake fake = {100, 0, 0};
    CoverYield yield = cover_yield_make(fake_now, fake_service, &fake);
    cover_yield_begin(&yield);
    fake.now = 103;
    CHECK(cover_yield_point(&yield));
    CHECK_EQUAL(0, fake.services);
    fake.now = 104;
    CHECK(cover_yield_point(&yield));
    CHECK_EQUAL(1, fake.services);
    fake.now = 107;
    CHECK(cover_yield_point(&yield));
    CHECK_EQUAL(1, fake.services);
}

static void the_longest_stretch_is_recorded(void)
{
    Fake fake = {0, 0, 0};
    CoverYield yield = cover_yield_make(fake_now, fake_service, &fake);
    cover_yield_begin(&yield);
    fake.now = 4;
    cover_yield_point(&yield);
    fake.now = 13;
    cover_yield_point(&yield);
    fake.now = 17;
    cover_yield_point(&yield);
    CHECK_EQUAL(3, fake.services);
    CHECK_EQUAL(9, yield.longest_ms);
}

static void a_refusal_cancels_and_stays_cancelled(void)
{
    Fake fake = {0, 0, 2};
    CoverYield yield = cover_yield_make(fake_now, fake_service, &fake);
    cover_yield_begin(&yield);
    fake.now = 4;
    CHECK(cover_yield_point(&yield));
    fake.now = 8;
    CHECK(!cover_yield_point(&yield));
    CHECK(yield.cancelled);
    fake.now = 9;
    CHECK(!cover_yield_point(&yield));
    CHECK_EQUAL(2, fake.services);
}

static void the_slice_can_be_changed(void)
{
    Fake fake = {0, 0, 0};
    CoverYield yield = cover_yield_make(fake_now, fake_service, &fake);
    yield.slice_ms = 10;
    cover_yield_begin(&yield);
    fake.now = 9;
    cover_yield_point(&yield);
    CHECK_EQUAL(0, fake.services);
    fake.now = 10;
    cover_yield_point(&yield);
    CHECK_EQUAL(1, fake.services);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_missing_hook_never_stops_the_work),
        TEST_CASE(the_hook_serves_only_after_a_full_slice),
        TEST_CASE(the_longest_stretch_is_recorded),
        TEST_CASE(a_refusal_cancels_and_stays_cancelled),
        TEST_CASE(the_slice_can_be_changed),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
