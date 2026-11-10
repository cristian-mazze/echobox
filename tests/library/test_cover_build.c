#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "cover_card.h"
#include "library/covers.h"

static const Color RED = {255, 0, 0, 0xF800};
static const Color GREEN = {0, 255, 0, 0x07E0};
static const Color BLUE = {0, 0, 255, 0x001F};
static const Color WHITE = {255, 255, 255, 0xFFFF};

static void three_albums(void)
{
    card_reset();
    card_add_album("Alpha", RED);
    card_add_album("Beta", GREEN);
    card_add_album("Gamma", BLUE);
}

static void the_first_build_decodes_every_album(void)
{
    CoverStats stats;
    three_albums();
    CHECK_EQUAL(SCAN_OK, card_rescan(true, &stats));
    CHECK_EQUAL(3u, stats.albums);
    CHECK_EQUAL(3u, stats.covers);
    CHECK_EQUAL(0u, stats.reused);
    CHECK_EQUAL(RED.pixel, card_cover_pixel("Alpha"));
    CHECK_EQUAL(GREEN.pixel, card_cover_pixel("Beta"));
    CHECK_EQUAL(BLUE.pixel, card_cover_pixel("Gamma"));
}

static void an_unchanged_card_reuses_every_cover(void)
{
    CoverStats stats;
    three_albums();
    CHECK_EQUAL(SCAN_OK, card_rescan(true, &stats));
    CHECK_EQUAL(SCAN_OK, card_rescan(true, &stats));
    CHECK_EQUAL(3u, stats.covers);
    CHECK_EQUAL(3u, stats.reused);
    CHECK_EQUAL(0u, stats.missing);
    CHECK_EQUAL(RED.pixel, card_cover_pixel("Alpha"));
    CHECK_EQUAL(GREEN.pixel, card_cover_pixel("Beta"));
    CHECK_EQUAL(BLUE.pixel, card_cover_pixel("Gamma"));
}

static void a_new_album_is_the_only_one_decoded(void)
{
    CoverStats stats;
    three_albums();
    CHECK_EQUAL(SCAN_OK, card_rescan(true, &stats));
    card_add_album("Delta", WHITE);
    CHECK_EQUAL(SCAN_OK, card_rescan(true, &stats));
    CHECK_EQUAL(4u, stats.covers);
    CHECK_EQUAL(3u, stats.reused);
    CHECK_EQUAL(WHITE.pixel, card_cover_pixel("Delta"));
    CHECK_EQUAL(BLUE.pixel, card_cover_pixel("Gamma"));
}

static void replaced_art_is_decoded_again(void)
{
    CoverStats stats;
    three_albums();
    CHECK_EQUAL(SCAN_OK, card_rescan(true, &stats));
    card_replace_cover("Beta", WHITE);
    CHECK_EQUAL(SCAN_OK, card_rescan(true, &stats));
    CHECK_EQUAL(3u, stats.covers);
    CHECK_EQUAL(2u, stats.reused);
    CHECK_EQUAL(WHITE.pixel, card_cover_pixel("Beta"));
    CHECK_EQUAL(RED.pixel, card_cover_pixel("Alpha"));
}

static void a_removed_album_frees_its_place_for_a_new_one(void)
{
    CoverStats stats;
    three_albums();
    card_covers_capacity = COVERS_HEADER_SIZE + 3 * CARD_RECORD_SLOT;
    CHECK_EQUAL(SCAN_OK, card_rescan(true, &stats));
    CHECK_EQUAL(3u, stats.covers);
    card_remove_album("Beta");
    card_add_album("Delta", WHITE);
    CHECK_EQUAL(SCAN_OK, card_rescan(true, &stats));
    CHECK_EQUAL(3u, stats.covers);
    CHECK_EQUAL(2u, stats.reused);
    CHECK_EQUAL(0u, stats.missing);
    CHECK_EQUAL(WHITE.pixel, card_cover_pixel("Delta"));
    CHECK_EQUAL(RED.pixel, card_cover_pixel("Alpha"));
    CHECK_EQUAL(BLUE.pixel, card_cover_pixel("Gamma"));
}

static void an_album_without_art_loses_its_cover(void)
{
    CoverStats stats;
    three_albums();
    CHECK_EQUAL(SCAN_OK, card_rescan(true, &stats));
    card_remove_album("Beta");
    card_add_bare_album("Beta");
    CHECK_EQUAL(SCAN_OK, card_rescan(true, &stats));
    CHECK_EQUAL(2u, stats.covers);
    CHECK_EQUAL(1u, stats.missing);
    CHECK_EQUAL(0u, card_cover_pixel("Beta"));
}

static void a_file_without_stamps_is_built_again(void)
{
    CoverStats stats;
    three_albums();
    CHECK_EQUAL(SCAN_OK, card_rescan(true, &stats));
    memset(card_covers.bytes + 12, 0, 4);
    CHECK_EQUAL(SCAN_OK, card_rescan(true, &stats));
    CHECK_EQUAL(3u, stats.covers);
    CHECK_EQUAL(0u, stats.reused);
    CHECK_EQUAL(GREEN.pixel, card_cover_pixel("Beta"));
}

static void a_damaged_index_is_ignored(void)
{
    CoverStats stats;
    uint8_t first[8];
    three_albums();
    CHECK_EQUAL(SCAN_OK, card_rescan(true, &stats));
    memcpy(first, card_covers.bytes + COVERS_HEADER_SIZE, sizeof first);
    memcpy(card_covers.bytes + COVERS_HEADER_SIZE, card_covers.bytes + COVERS_HEADER_SIZE + 8, 8);
    memcpy(card_covers.bytes + COVERS_HEADER_SIZE + 8, first, sizeof first);
    CHECK_EQUAL(SCAN_OK, card_rescan(true, &stats));
    CHECK_EQUAL(3u, stats.covers);
    CHECK_EQUAL(0u, stats.reused);
    CHECK_EQUAL(RED.pixel, card_cover_pixel("Alpha"));
    CHECK_EQUAL(GREEN.pixel, card_cover_pixel("Beta"));
}

static void without_a_previous_file_everything_is_decoded(void)
{
    CoverStats stats;
    three_albums();
    CHECK_EQUAL(SCAN_OK, card_rescan(true, &stats));
    CHECK_EQUAL(SCAN_OK, card_rescan(false, &stats));
    CHECK_EQUAL(3u, stats.covers);
    CHECK_EQUAL(0u, stats.reused);
}

static const Color YELLOW = {255, 255, 0, 0xFFE0};
static const Color CYAN = {0, 255, 255, 0x07FF};
static const Color MAGENTA = {255, 0, 255, 0xF81F};

typedef struct Expectation {
    const char *title;
    uint16_t before;
    uint16_t after;
} Expectation;

typedef struct CutSweep {
    uint32_t cuts;
    uint32_t record_cuts;
    uint32_t worst_record_decoded;
    uint32_t worst_table_decoded;
} CutSweep;

static uint16_t shown_pixel(const char *title)
{
    static uint16_t pixels[COVER_FULL_PIXELS];
    Covers covers;
    uint32_t record = 0;
    if (!covers_open(&covers, (ByteSource){card_buffer_read, &card_covers}, card_covers_capacity) ||
        !covers_find(&covers, card_album_key(title), &record)) {
        return 0;
    }
    CHECK(covers_read(&covers, record, COVER_FULL, pixels));
    return pixels[COVER_FULL_PIXELS / 2];
}

static void five_albums(void)
{
    card_reset();
    card_add_album("Alpha", RED);
    card_add_album("Beta", GREEN);
    card_add_album("Gamma", BLUE);
    card_add_album("Delta", WHITE);
    card_add_album("Epsilon", YELLOW);
}

static void check_no_wrong_cover(const Expectation *expected, uint32_t count)
{
    for (uint32_t index = 0; index < count; index++) {
        uint16_t pixel = shown_pixel(expected[index].title);
        CHECK(pixel == 0 || pixel == expected[index].before || pixel == expected[index].after);
    }
}

static void check_final_covers(const Expectation *expected, uint32_t count)
{
    for (uint32_t index = 0; index < count; index++) {
        CHECK_EQUAL(expected[index].after, card_cover_pixel(expected[index].title));
    }
}

static void recover_after_cut(const Expectation *expected, uint32_t count, CutSweep *sweep,
                              bool in_record)
{
    CoverStats stats;
    card_cut = (CutSink){.left = -1};
    check_no_wrong_cover(expected, count);
    CHECK_EQUAL(SCAN_OK, card_rescan(true, &stats));
    CHECK_EQUAL(count, stats.reused + stats.decoded);
    check_final_covers(expected, count);
    uint32_t *worst = in_record ? &sweep->worst_record_decoded : &sweep->worst_table_decoded;
    *worst = stats.decoded > *worst ? stats.decoded : *worst;
    sweep->record_cuts += in_record ? 1u : 0u;
}

static CutSweep sweep_cuts(const uint8_t *snapshot, const Expectation *expected, uint32_t count)
{
    CoverStats stats;
    CutSweep sweep = {0, 0, 0, 0};
    uint32_t records_start = COVERS_HEADER_SIZE + (card_covers_capacity - COVERS_HEADER_SIZE) /
                                                      CARD_RECORD_SLOT *
                                                      (CARD_RECORD_SLOT - COVER_BUILD_RECORD_BYTES);
    memcpy(card_covers.bytes, snapshot, card_covers_capacity);
    card_cut = (CutSink){.left = -1};
    CHECK_EQUAL(SCAN_OK, card_rescan(true, &stats));
    uint32_t writes = card_cut.writes;
    for (uint32_t cut = 0; cut < writes; cut++) {
        memcpy(card_covers.bytes, snapshot, card_covers_capacity);
        card_cut = (CutSink){.left = (int32_t)cut};
        CHECK_EQUAL(SCAN_WRITE_FAILED, card_rescan(true, &stats));
        recover_after_cut(expected, count, &sweep, card_cut.failed_at >= records_start);
        sweep.cuts++;
    }
    return sweep;
}

static uint8_t *built_snapshot(void)
{
    CoverStats stats;
    uint8_t *snapshot = malloc(CARD_COVERS_CAPACITY);
    CHECK_EQUAL(SCAN_OK, card_rescan(true, &stats));
    CHECK_EQUAL(5u, stats.decoded);
    memcpy(snapshot, card_covers.bytes, card_covers_capacity);
    return snapshot;
}

static void report_sweep(const char *name, CutSweep sweep)
{
    printf("%s: %u cuts, %u inside records (worst recovery decoded %u), %u in the index "
           "(worst recovery decoded %u)\n",
           name, sweep.cuts, sweep.record_cuts, sweep.worst_record_decoded,
           sweep.cuts - sweep.record_cuts, sweep.worst_table_decoded);
}

static void a_cut_anywhere_never_shows_another_albums_cover(void)
{
    static const Expectation expected[] = {
        {"Alpha", 0xF800, 0xF800},   {"Beta", 0x07E0, 0x07FF}, {"Delta", 0xFFFF, 0xFFFF},
        {"Epsilon", 0xFFE0, 0xFFE0}, {"Zeta", 0, 0xF81F},
    };
    five_albums();
    uint8_t *snapshot = built_snapshot();
    card_remove_album("Gamma");
    card_replace_cover("Beta", CYAN);
    card_add_album("Zeta", MAGENTA);
    CutSweep sweep = sweep_cuts(snapshot, expected, 5);
    report_sweep("roomy file", sweep);
    CHECK(sweep.cuts > 0);
    CHECK(sweep.worst_record_decoded <= 2u);
    free(snapshot);
}

static void a_cut_while_rewriting_a_full_file_never_shows_another_albums_cover(void)
{
    static const Expectation expected[] = {
        {"Alpha", 0xF800, 0xF800}, {"Beta", 0x07E0, 0x07FF},    {"Gamma", 0x001F, 0x001F},
        {"Delta", 0xFFFF, 0xFFFF}, {"Epsilon", 0xFFE0, 0xFFE0},
    };
    five_albums();
    card_covers_capacity = COVERS_HEADER_SIZE + 5 * CARD_RECORD_SLOT;
    uint8_t *snapshot = built_snapshot();
    card_replace_cover("Beta", CYAN);
    CutSweep sweep = sweep_cuts(snapshot, expected, 5);
    report_sweep("full file", sweep);
    CHECK(sweep.cuts > 0);
    CHECK(sweep.worst_record_decoded <= 1u);
    free(snapshot);
}

static void art_changed_between_the_passes_keeps_its_real_stamp(void)
{
    CoverStats stats;
    three_albums();
    CHECK_EQUAL(SCAN_OK, card_rescan(true, &stats));
    card_add_album("Delta", WHITE);
    card_scan();
    card_replace_cover("Delta", RED);
    CHECK_EQUAL(SCAN_OK, card_cover_pass(true, &stats));
    CHECK_EQUAL(1u, stats.decoded);
    CHECK_EQUAL(RED.pixel, card_cover_pixel("Delta"));
    CHECK_EQUAL(SCAN_OK, card_rescan(true, &stats));
    CHECK_EQUAL(0u, stats.decoded);
    CHECK_EQUAL(4u, stats.reused);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_first_build_decodes_every_album),
        TEST_CASE(an_unchanged_card_reuses_every_cover),
        TEST_CASE(a_new_album_is_the_only_one_decoded),
        TEST_CASE(replaced_art_is_decoded_again),
        TEST_CASE(a_removed_album_frees_its_place_for_a_new_one),
        TEST_CASE(an_album_without_art_loses_its_cover),
        TEST_CASE(a_file_without_stamps_is_built_again),
        TEST_CASE(a_damaged_index_is_ignored),
        TEST_CASE(without_a_previous_file_everything_is_decoded),
        TEST_CASE(art_changed_between_the_passes_keeps_its_real_stamp),
        TEST_CASE(a_cut_anywhere_never_shows_another_albums_cover),
        TEST_CASE(a_cut_while_rewriting_a_full_file_never_shows_another_albums_cover),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
