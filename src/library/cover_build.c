#include "library/cover_build.h"

#include <stddef.h>

#include "art_facts.h"
#include "core/hash.h"
#include "cover_art_rules.h"
#include "cover_index.h"
#include "image/cover_image.h"
#include "library/covers.h"
#include "library/library.h"
#include "library/track_tags.h"

enum {
    MAX_FOLDERS = 16,
    FULL_BYTES = COVER_FULL_PIXELS * 2,
    HALF_BYTES = COVER_HALF_PIXELS * 2,
    STAMP_NUMBERS = 3,
    ART_EMBEDDED = 1,
    ART_FILE = 2
};

typedef struct ArtFile {
    ByteSource source;
    uint32_t offset;
    uint32_t length;
    uint32_t stamp;
} ArtFile;

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

typedef struct CoverState {
    Library library;
    TrackTags tags;
    ScanEntry entry;
    LibraryAlbum album;
    uint32_t key;
    char path[SCAN_PATH_CAPACITY + 2];
    CoverArtPick pick;
    ArtFacts facts;
    uint16_t full[COVER_FULL_PIXELS];
    uint16_t half[COVER_HALF_PIXELS];
    uint16_t folders[MAX_FOLDERS];
    uint32_t folder_count;
    CoverIndex index;
    uint8_t *decode_memory;
    uint32_t decode_size;
} CoverState;

static uint32_t index_capacity(CoverLayout layout, uint32_t albums, uint32_t previous)
{
    uint32_t limit = layout.slots < albums ? layout.slots : albums;
    return limit > previous ? limit : previous;
}

typedef enum Placement {
    PLACEMENT_DONE,
    PLACEMENT_SKIPPED,
    PLACEMENT_FAILED
} Placement;

static CoverState *prepare(const CoverJob *job, uint32_t albums, CoverLayout layout,
                           uint32_t previous)
{
    CoverArena arena = {job->memory, (uint8_t *)job->memory + job->size};
    CoverState *state = cover_arena_take(&arena, sizeof(CoverState));
    uint32_t capacity = index_capacity(layout, albums, previous);
    if (state == NULL || !cover_index_carve(&state->index, &arena, layout, capacity)) {
        return NULL;
    }
    state->index.sink = job->sink;
    state->decode_memory = arena.cursor;
    state->decode_size = (uint32_t)(arena.end - arena.cursor);
    return state;
}

static bool write_bytes(const CoverJob *job, uint32_t offset, const void *bytes, uint32_t length)
{
    return job->sink.write(job->sink.context, offset, bytes, length);
}

static void put16(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *bytes, uint32_t value)
{
    put16(bytes, value);
    put16(bytes + 2, value >> 16);
}

static uint32_t text_length(const char *text)
{
    uint32_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static uint32_t art_stamp(const char *path, uint32_t first, uint32_t second, uint32_t third)
{
    uint8_t numbers[STAMP_NUMBERS * 4];
    put32(numbers, first);
    put32(numbers + 4, second);
    put32(numbers + 8, third);
    uint32_t hash = hash_update(HASH_START, path, text_length(path));
    return hash_update(hash, numbers, sizeof numbers);
}

static bool read_track_path(CoverState *state, uint32_t track_index, LibraryTrack *track)
{
    state->path[0] = '/';
    return library_read_track(&state->library, track_index, track) &&
           library_read_string(&state->library, track->path, state->path + 1,
                               sizeof state->path - 1);
}

static bool open_embedded(CoverState *state, const CoverJob *job, ArtFile *art)
{
    LibraryTrack track;
    uint32_t size = 0;
    if (!read_track_path(state, state->album.first_track, &track) ||
        !job->files.open_file(job->files.context, state->path, &art->source, &size)) {
        return false;
    }
    TagFormat format = track_tags_format_for_name(state->path);
    bool tagged = track_tags_read(art->source, size, format, &state->tags);
    TagPicture picture = state->tags.picture;
    if (!tagged || picture.kind != TAG_PICTURE_RAW || picture.length == 0) {
        job->files.close_file(job->files.context);
        return false;
    }
    art->offset = picture.offset;
    art->length = picture.length;
    art->stamp = art_stamp(state->path, size, picture.length, ART_EMBEDDED);
    return true;
}

static uint32_t folder_length(const char *path)
{
    uint32_t slash = 0;
    for (uint32_t index = 0; path[index] != '\0'; index++) {
        slash = path[index] == '/' ? index : slash;
    }
    return slash;
}

static void cut_to_folder(CoverState *state)
{
    uint32_t length = folder_length(state->path);
    state->path[length > 0 ? length : 1] = '\0';
}

static bool best_match(CoverState *state, const CoverJob *job)
{
    cut_to_folder(state);
    cover_art_pick_reset(&state->pick);
    if (!job->files.open_dir(job->files.context, state->path)) {
        return false;
    }
    while (job->files.next_entry(job->files.context, &state->entry)) {
        cover_art_pick_offer(&state->pick, &state->entry);
    }
    job->files.close_dir(job->files.context);
    return cover_art_pick_found(&state->pick);
}

static bool append_name(CoverState *state, const char *name)
{
    uint32_t length = text_length(state->path);
    if (length > 1) {
        state->path[length++] = '/';
    }
    for (uint32_t index = 0; name[index] != '\0'; index++) {
        if (length + 1 >= sizeof state->path) {
            return false;
        }
        state->path[length++] = name[index];
    }
    state->path[length] = '\0';
    return true;
}

static bool open_art_file(CoverState *state, const CoverJob *job, ArtFile *art)
{
    uint32_t size = 0;
    if (!append_name(state, state->pick.name) ||
        !job->files.open_file(job->files.context, state->path, &art->source, &size)) {
        return false;
    }
    art->offset = 0;
    art->length = size;
    art->stamp = art_stamp(state->path, size, state->pick.modified, ART_FILE);
    return true;
}

static bool folder_tried(CoverState *state, uint16_t folder)
{
    for (uint32_t index = 0; index < state->folder_count; index++) {
        if (state->folders[index] == folder) {
            return true;
        }
    }
    if (state->folder_count < MAX_FOLDERS) {
        state->folders[state->folder_count++] = folder;
    }
    return false;
}

static bool open_folder_art(CoverState *state, const CoverJob *job, ArtFile *art)
{
    const LibraryAlbum *album = &state->album;
    state->folder_count = 0;
    for (uint32_t track = 0; track < album->track_count; track++) {
        LibraryTrack info;
        if (!read_track_path(state, album->first_track + track, &info) ||
            folder_tried(state, info.folder)) {
            continue;
        }
        if (best_match(state, job)) {
            return open_art_file(state, job, art);
        }
    }
    return false;
}

static bool open_art(CoverState *state, const CoverJob *job, ArtFile *art)
{
    if (state->album.track_count == 0) {
        return false;
    }
    return open_embedded(state, job, art) || open_folder_art(state, job, art);
}

static bool embedded_fact(CoverState *state, ExpectedArt *expected)
{
    ArtTrackFact fact;
    LibraryTrack track;
    if (!art_facts_track(&state->facts, state->album.first_track, &fact)) {
        expected->kind = ART_UNKNOWN;
        return true;
    }
    if (fact.picture_kind != TAG_PICTURE_RAW || fact.picture_length == 0) {
        return false;
    }
    if (!read_track_path(state, state->album.first_track, &track)) {
        expected->kind = ART_UNKNOWN;
        return true;
    }
    expected->kind = ART_EXPECTED;
    expected->embedded = true;
    expected->stamp = art_stamp(state->path, fact.size, fact.picture_length, ART_EMBEDDED);
    return true;
}

static bool folder_pick_of(CoverState *state, uint32_t track, bool *known)
{
    ArtTrackFact fact;
    *known = art_facts_track(&state->facts, track, &fact);
    if (!*known || folder_tried(state, fact.folder)) {
        return false;
    }
    *known = art_facts_folder(&state->facts, fact.folder, &state->pick);
    return *known && cover_art_pick_found(&state->pick);
}

static void expect_folder_art(CoverState *state, uint32_t track, ExpectedArt *expected)
{
    LibraryTrack info;
    expected->kind = ART_UNKNOWN;
    if (!read_track_path(state, track, &info)) {
        return;
    }
    cut_to_folder(state);
    if (append_name(state, state->pick.name)) {
        expected->kind = ART_EXPECTED;
        expected->embedded = false;
        expected->stamp = art_stamp(state->path, state->pick.size, state->pick.modified, ART_FILE);
    }
}

static ExpectedArt expect_art(CoverState *state)
{
    ExpectedArt expected = {.kind = ART_UNKNOWN};
    if (!state->facts.valid || embedded_fact(state, &expected)) {
        return expected;
    }
    expected.kind = ART_ABSENT;
    state->folder_count = 0;
    for (uint32_t index = 0; index < state->album.track_count; index++) {
        bool known = true;
        uint32_t track = state->album.first_track + index;
        if (folder_pick_of(state, track, &known)) {
            expect_folder_art(state, track, &expected);
            return expected;
        }
        if (!known) {
            expected.kind = ART_UNKNOWN;
            return expected;
        }
    }
    return expected;
}

static bool open_expected(CoverState *state, const CoverJob *job, const ExpectedArt *expected,
                          ArtFile *art)
{
    if (expected->kind != ART_EXPECTED) {
        return false;
    }
    return expected->embedded ? open_embedded(state, job, art) : open_folder_art(state, job, art);
}

static bool decode_art(CoverState *state, const ArtFile *art)
{
    ImageSource image = {art->source.read, art->source.context, art->offset, art->length};
    return cover_image_decode(image, state->decode_memory, state->decode_size, state->full,
                              state->half) == COVER_IMAGE_OK;
}

static void pack_pixels(uint16_t *pixels, uint32_t count)
{
    uint8_t *bytes = (uint8_t *)pixels;
    for (uint32_t index = 0; index < count; index++) {
        uint16_t value = pixels[index];
        put16(bytes + index * 2, value);
    }
}

static bool write_record(CoverState *state, const CoverJob *job, uint32_t slot)
{
    uint32_t offset = cover_index_record_offset(&state->index, slot);
    pack_pixels(state->full, COVER_FULL_PIXELS);
    pack_pixels(state->half, COVER_HALF_PIXELS);
    return write_bytes(job, offset, state->full, FULL_BYTES) &&
           write_bytes(job, offset + FULL_BYTES, state->half, HALF_BYTES);
}

static Placement add_cover(CoverState *state, const CoverJob *job, uint32_t position,
                           uint32_t stamp)
{
    uint32_t slot = 0;
    if (!cover_index_free_slot(&state->index, &slot)) {
        return PLACEMENT_SKIPPED;
    }
    bool added = write_record(state, job, slot) &&
                 cover_index_add(&state->index, position, (CoverEntry){state->key, stamp}, slot);
    return added ? PLACEMENT_DONE : PLACEMENT_FAILED;
}

static Placement swap_cover(CoverState *state, const CoverJob *job, uint32_t position,
                            uint32_t stamp)
{
    CoverEntry entry = {state->key, stamp};
    uint32_t slot = 0;
    if (cover_index_free_slot(&state->index, &slot)) {
        bool swapped = write_record(state, job, slot) &&
                       cover_index_replace(&state->index, position, entry, slot);
        return swapped ? PLACEMENT_DONE : PLACEMENT_FAILED;
    }
    bool rewritten = cover_index_retire(&state->index, position, &slot) &&
                     write_record(state, job, slot) &&
                     cover_index_add(&state->index, position, entry, slot);
    return rewritten ? PLACEMENT_DONE : PLACEMENT_FAILED;
}

static Placement decode_cover(CoverState *state, const CoverJob *job, const ArtFile *art)
{
    uint32_t position = 0;
    bool known = cover_index_find(&state->index, state->key, &position);
    if ((!known && !cover_index_has_room(&state->index)) || !decode_art(state, art)) {
        return PLACEMENT_SKIPPED;
    }
    return known ? swap_cover(state, job, position, art->stamp)
                 : add_cover(state, job, position, art->stamp);
}

static ScanResult place_cover(CoverState *state, const CoverJob *job, const ArtFile *art,
                              CoverStats *stats)
{
    uint32_t position = 0;
    if (cover_index_find(&state->index, state->key, &position) &&
        cover_index_reusable(&state->index, position, art->stamp)) {
        cover_index_claim(&state->index, position);
        stats->reused++;
        return SCAN_OK;
    }
    Placement placement = decode_cover(state, job, art);
    if (placement == PLACEMENT_FAILED) {
        return SCAN_WRITE_FAILED;
    }
    stats->decoded += placement == PLACEMENT_DONE ? 1u : 0u;
    stats->missing += placement == PLACEMENT_SKIPPED ? 1u : 0u;
    return SCAN_OK;
}

static bool reuse_expected(CoverState *state, const ExpectedArt *expected, CoverStats *stats)
{
    uint32_t position = 0;
    if (expected->kind != ART_EXPECTED || !cover_index_find(&state->index, state->key, &position) ||
        !cover_index_reusable(&state->index, position, expected->stamp)) {
        return false;
    }
    cover_index_claim(&state->index, position);
    stats->reused++;
    return true;
}

static ScanResult cover_album(CoverState *state, const CoverJob *job, CoverStats *stats)
{
    ExpectedArt expected = expect_art(state);
    if (expected.kind == ART_ABSENT) {
        stats->missing++;
        return SCAN_OK;
    }
    if (reuse_expected(state, &expected, stats)) {
        return SCAN_OK;
    }
    ArtFile art;
    if (!open_expected(state, job, &expected, &art) && !open_art(state, job, &art)) {
        stats->missing++;
        return SCAN_OK;
    }
    ScanResult result = place_cover(state, job, &art, stats);
    job->files.close_file(job->files.context);
    return result;
}

static bool already_placed(const CoverState *state, uint32_t key)
{
    uint32_t position = 0;
    return cover_index_find(&state->index, key, &position) &&
           cover_index_claimed(&state->index, position);
}

static bool keep_present(CoverState *state, uint32_t albums)
{
    for (uint32_t album_index = 0; album_index < albums; album_index++) {
        uint32_t position = 0;
        if (cover_index_find(&state->index, library_album_key(&state->library, album_index),
                             &position)) {
            cover_index_claim(&state->index, position);
        }
    }
    return cover_index_drop_unclaimed(&state->index);
}

static void report(const CoverJob *job, uint32_t done, uint32_t total)
{
    if (job->progress.report != NULL) {
        job->progress.report(job->progress.context, done, total);
    }
}

static ScanResult build_albums(CoverState *state, const CoverJob *job, CoverStats *stats)
{
    for (uint32_t album_index = 0; album_index < stats->albums; album_index++) {
        report(job, album_index, stats->albums);
        state->key = library_album_key(&state->library, album_index);
        if (!library_read_album(&state->library, album_index, &state->album) ||
            already_placed(state, state->key)) {
            continue;
        }
        ScanResult result = cover_album(state, job, stats);
        if (result != SCAN_OK) {
            return result;
        }
    }
    report(job, stats->albums, stats->albums);
    return SCAN_OK;
}

static ScanResult finish(CoverState *state, CoverStats *stats, ScanResult result)
{
    if (result == SCAN_OK && !cover_index_drop_unclaimed(&state->index)) {
        result = SCAN_WRITE_FAILED;
    }
    stats->covers = state->index.count;
    return result;
}

static CoverState *start(const CoverJob *job, uint32_t albums, CoverLayout layout)
{
    uint32_t previous = cover_index_previous_count(job->previous, job->capacity, layout);
    CoverState *state = prepare(job, albums, layout, previous);
    if (state == NULL && previous > 0) {
        previous = 0;
        state = prepare(job, albums, layout, previous);
    }
    if (state != NULL) {
        cover_index_load(&state->index, job->previous, previous);
    }
    return state;
}

static bool settle_previous(CoverState *state, uint32_t albums)
{
    if (state->index.count == 0) {
        return cover_index_clear(&state->index);
    }
    return keep_present(state, albums);
}

ScanResult cover_build(const CoverJob *job, CoverStats *stats)
{
    *stats = (CoverStats){.albums = 0, .covers = 0, .reused = 0, .missing = 0, .decoded = 0};
    Library probe;
    if (library_open(&probe, job->library, job->library_size) != LIBRARY_OK) {
        return SCAN_NO_MUSIC_ROOT;
    }
    stats->albums = library_count(&probe, LIBRARY_ALBUMS);
    CoverLayout layout = cover_layout_for(job->capacity);
    CoverState *state = start(job, stats->albums, layout);
    if (state == NULL) {
        return SCAN_OUT_OF_MEMORY;
    }
    if (layout.slots == 0 && stats->albums > 0) {
        return SCAN_NO_ROOM;
    }
    state->library = probe;
    art_facts_load(&state->facts, job->scratch, job->scratch_size,
                   library_count(&probe, LIBRARY_TRACKS));
    if (!settle_previous(state, stats->albums)) {
        return SCAN_WRITE_FAILED;
    }
    return finish(state, stats, build_albums(state, job, stats));
}
