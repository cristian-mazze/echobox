#include "check.h"
#include "cover_cache.h"
#include "fake_hal.h"
#include "fixture_library.h"

enum {
    RED_ALBUM = 0,
    BARE_ALBUM = 1,
    BLUE_ALBUM = 2,
    RED = 0xF800,
    BLUE = 0x001F
};

static FixtureLibrary fixture;
static CoverCache cache;

static CoverCache *cache_with_covers(void)
{
    uint32_t size = 0;
    const uint8_t *bytes = fixture_covers_bytes(&size);
    fake_hal_reset();
    fake_hal_add_file("/.echo/covers.bin", bytes, size);
    cover_cache_open(&cache, fixture_library_open(&fixture));
    return &cache;
}

static void without_a_covers_file_every_cover_is_missing(void)
{
    fake_hal_reset();
    cover_cache_open(&cache, fixture_library_open(&fixture));
    CHECK(!cache.available);
    Image image;
    cover_cache_load_half(&cache, 0, RED_ALBUM);
    CHECK_EQUAL(COVER_MISSING, cover_cache_half(&cache, 0, RED_ALBUM, &image));
}

static void an_unloaded_cover_is_empty(void)
{
    CoverCache *covers = cache_with_covers();
    Image image;
    CHECK(covers->available);
    CHECK_EQUAL(COVER_EMPTY, cover_cache_half(covers, 0, RED_ALBUM, &image));
    CHECK_EQUAL(COVER_EMPTY, cover_cache_full(covers, RED_ALBUM, &image));
}

static void a_loaded_half_cover_has_its_pixels(void)
{
    CoverCache *covers = cache_with_covers();
    Image image;
    CHECK(cover_cache_load_half(covers, 3, BLUE_ALBUM));
    CHECK_EQUAL(COVER_LOADED, cover_cache_half(covers, 3, BLUE_ALBUM, &image));
    CHECK_EQUAL(COVER_HALF_SIZE, image.width);
    CHECK_EQUAL(BLUE, image.pixels[0]);
    CHECK(!cover_cache_load_half(covers, 3, BLUE_ALBUM));
}

static void a_loaded_full_cover_has_its_pixels(void)
{
    CoverCache *covers = cache_with_covers();
    Image image;
    CHECK(cover_cache_load_full(covers, RED_ALBUM));
    CHECK_EQUAL(COVER_LOADED, cover_cache_full(covers, RED_ALBUM, &image));
    CHECK_EQUAL(COVER_FULL_SIZE, image.height);
    CHECK_EQUAL(RED, image.pixels[COVER_FULL_PIXELS - 1]);
    CHECK_EQUAL(COVER_EMPTY, cover_cache_full(covers, BLUE_ALBUM, &image));
}

static void an_album_without_art_is_missing(void)
{
    CoverCache *covers = cache_with_covers();
    Image image;
    CHECK(cover_cache_load_half(covers, 1, BARE_ALBUM));
    CHECK_EQUAL(COVER_MISSING, cover_cache_half(covers, 1, BARE_ALBUM, &image));
}

static void a_position_reuses_the_slot_of_one_a_window_away(void)
{
    CoverCache *covers = cache_with_covers();
    Image image;
    cover_cache_load_half(covers, 0, RED_ALBUM);
    cover_cache_load_half(covers, COVER_CACHE_SLOTS, BLUE_ALBUM);
    CHECK_EQUAL(COVER_EMPTY, cover_cache_half(covers, 0, RED_ALBUM, &image));
    CHECK_EQUAL(COVER_LOADED, cover_cache_half(covers, COVER_CACHE_SLOTS, BLUE_ALBUM, &image));
}

static void covers_written_after_opening_show_after_a_refresh(void)
{
    uint32_t size = 0;
    const uint8_t *bytes = fixture_covers_bytes(&size);
    Image image;
    fake_hal_reset();
    cover_cache_open(&cache, fixture_library_open(&fixture));
    CHECK(!cover_cache_has(&cache, RED_ALBUM));
    fake_hal_add_file("/.echo/covers.bin", bytes, size);
    cover_cache_refresh(&cache);
    CHECK(cover_cache_has(&cache, RED_ALBUM));
    CHECK(cover_cache_load_full(&cache, RED_ALBUM));
    CHECK_EQUAL(COVER_LOADED, cover_cache_full(&cache, RED_ALBUM, &image));
    CHECK_EQUAL(RED, image.pixels[0]);
}

static void a_full_load_without_a_cover_is_reported_once(void)
{
    CoverCache *covers = cache_with_covers();
    uint16_t album = 0;
    CHECK(!cover_cache_take_missed(covers, &album));
    cover_cache_load_full(covers, RED_ALBUM);
    CHECK(!cover_cache_take_missed(covers, &album));
    cover_cache_load_full(covers, BARE_ALBUM);
    CHECK(cover_cache_take_missed(covers, &album));
    CHECK_EQUAL(BARE_ALBUM, album);
    CHECK(!cover_cache_take_missed(covers, &album));
}

static void a_noted_album_is_reported_only_without_a_cover(void)
{
    CoverCache *covers = cache_with_covers();
    uint16_t album = 0;
    cover_cache_note_album(covers, BLUE_ALBUM);
    CHECK(!cover_cache_take_missed(covers, &album));
    cover_cache_note_album(covers, BARE_ALBUM);
    CHECK(cover_cache_take_missed(covers, &album));
    CHECK_EQUAL(BARE_ALBUM, album);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(without_a_covers_file_every_cover_is_missing),
        TEST_CASE(an_unloaded_cover_is_empty),
        TEST_CASE(a_loaded_half_cover_has_its_pixels),
        TEST_CASE(a_loaded_full_cover_has_its_pixels),
        TEST_CASE(an_album_without_art_is_missing),
        TEST_CASE(a_position_reuses_the_slot_of_one_a_window_away),
        TEST_CASE(covers_written_after_opening_show_after_a_refresh),
        TEST_CASE(a_full_load_without_a_cover_is_reported_once),
        TEST_CASE(a_noted_album_is_reported_only_without_a_cover),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
