#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "art_facts.h"
#include "cover_art_rules.h"
#include "cover_index.h"
#include "library/cover_build.h"
#include "library/covers.h"
#include "library/library.h"
#include "library/track_tags.h"

enum {
    COVER_WORK_MAX_FOLDERS = 16,
    COVER_WORK_NO_ALBUM = 0x7FFFFFFF
};

typedef enum ArtExpectation {
    ART_UNKNOWN,
    ART_ABSENT,
    ART_EXPECTED
} ArtExpectation;

typedef struct ExpectedArt {
    ArtExpectation kind;
    bool embedded;
    uint32_t stamp;
} ExpectedArt;

typedef struct CoverGuard {
    ByteSource inner;
    const bool *cancel;
} CoverGuard;

typedef struct CoverState {
    Library library;
    TrackTags tags;
    ScanEntry entry;
    LibraryAlbum album;
    uint32_t key;
    char path[SCAN_PATH_CAPACITY + 2];
    CoverArtPick pick;
    ArtFacts facts;
    ExpectedArt expected;
    CoverGuard guard;
    const CoverPlan *plan;
    uint16_t full[COVER_FULL_PIXELS];
    uint16_t half[COVER_HALF_PIXELS];
    uint16_t folders[COVER_WORK_MAX_FOLDERS];
    uint32_t folder_count;
    CoverIndex index;
    uint8_t *pending;
    uint32_t albums;
    uint32_t anchor;
    uint32_t attempts;
    uint32_t pending_new;
    uint32_t pending_known;
    uint32_t started_ms;
    uint8_t *decode_memory;
    uint32_t decode_size;
} CoverState;

CoverState *cover_work_start(const CoverJob *job, const CoverPlan *plan, CoverStats *stats,
                             ScanResult *result);
bool cover_work_select(CoverState *state, uint32_t album_index);
bool cover_work_placed(const CoverState *state);
void cover_work_expect(CoverState *state, const CoverJob *job);
bool cover_work_reuse(CoverState *state, CoverStats *stats);
bool cover_work_keep(CoverState *state);
bool cover_work_failed(const CoverState *state, uint32_t stamp);
ScanResult cover_work_decode(CoverState *state, const CoverJob *job, CoverStats *stats);
bool cover_work_settle(CoverState *state);
bool cover_work_drop(CoverState *state);
bool cover_work_cancelled(const CoverState *state);
bool cover_work_budget_left(const CoverState *state);
void cover_work_report(const CoverJob *job, uint32_t done, uint32_t total);
bool cover_work_pending(const CoverState *state, uint32_t album_index);
void cover_work_set_pending(CoverState *state, uint32_t album_index, bool pending);
