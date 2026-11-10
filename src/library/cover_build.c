#include "library/cover_build.h"

#include <stddef.h>

#include "core/hash.h"
#include "cover_work.h"
#include "image/cover_image.h"
#include "library/cover_state.h"

enum {
    MAX_FOLDERS = COVER_WORK_MAX_FOLDERS,
    FULL_BYTES = COVER_FULL_PIXELS * 2,
    HALF_BYTES = COVER_HALF_PIXELS * 2,
    STAMP_NUMBERS = 3,
    ART_EMBEDDED = 1,
    ART_FILE = 2,
    BITS_PER_BYTE = 8
};

typedef struct ArtFile {
    ByteSource source;
    uint32_t offset;
    uint32_t length;
    uint32_t stamp;
} ArtFile;

typedef enum Placement {
    PLACEMENT_DONE,
    PLACEMENT_NO_ROOM,
    PLACEMENT_UNDECODABLE,
    PLACEMENT_FAILED
} Placement;

static uint32_t index_capacity(CoverLayout layout, uint32_t albums, uint32_t previous)
{
    uint32_t limit = layout.slots < albums ? layout.slots : albums;
    return limit > previous ? limit : previous;
}

static uint32_t bit_bytes(uint32_t bits)
{
    return (bits + BITS_PER_BYTE - 1) / BITS_PER_BYTE;
}

static void clear(uint8_t *bytes, uint32_t length)
{
    for (uint32_t index = 0; index < length; index++) {
        bytes[index] = 0;
    }
}

static CoverState *prepare(const CoverJob *job, uint32_t albums, CoverLayout layout,
                           uint32_t previous)
{
    CoverArena arena = {job->memory, (uint8_t *)job->memory + job->size};
    CoverState *state = cover_arena_take(&arena, sizeof(CoverState));
    uint8_t *pending = cover_arena_take(&arena, bit_bytes(albums) + 1);
    uint32_t capacity = index_capacity(layout, albums, previous);
    if (state == NULL || pending == NULL ||
        !cover_index_carve(&state->index, &arena, layout, capacity)) {
        return NULL;
    }
    clear(pending, bit_bytes(albums) + 1);
    state->pending = pending;
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
    if (expected->kind != ART_EXPECTED || !state->facts.valid) {
        return false;
    }
    return expected->embedded ? open_embedded(state, job, art) : open_folder_art(state, job, art);
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
        return PLACEMENT_NO_ROOM;
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

static bool guarded_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    const CoverGuard *guard = context;
    if (guard->cancel != NULL && *guard->cancel) {
        return false;
    }
    return guard->inner.read(guard->inner.context, offset, buffer, length);
}

static void guard_art(CoverState *state, ArtFile *art)
{
    state->guard.inner = art->source;
    state->guard.cancel = state->plan->cancel;
    art->source = (ByteSource){guarded_read, &state->guard};
}

static bool decode_art(CoverState *state, const ArtFile *art)
{
    ImageSource image = {art->source.read, art->source.context, art->offset, art->length};
    return cover_image_decode(image, state->decode_memory, state->decode_size, state->full,
                              state->half) == COVER_IMAGE_OK;
}

static Placement decode_cover(CoverState *state, const CoverJob *job, const ArtFile *art)
{
    uint32_t position = 0;
    bool known = cover_index_find(&state->index, state->key, &position);
    if (!known && !cover_index_has_room(&state->index)) {
        return PLACEMENT_NO_ROOM;
    }
    state->attempts++;
    if (!decode_art(state, art)) {
        return PLACEMENT_UNDECODABLE;
    }
    return known ? swap_cover(state, job, position, art->stamp)
                 : add_cover(state, job, position, art->stamp);
}

static void note_failure(CoverState *state, uint32_t stamp)
{
    if (state->plan->state != NULL) {
        cover_state_note_failure(state->plan->state, state->key, stamp);
    }
}

static void forget_failure(CoverState *state)
{
    if (state->plan->state != NULL) {
        cover_state_forget_failure(state->plan->state, state->key);
    }
}

static ScanResult drop_undecodable(CoverState *state, uint32_t stamp, CoverStats *stats)
{
    uint32_t position = 0;
    uint32_t slot = 0;
    if (cover_work_cancelled(state)) {
        return SCAN_CANCELLED;
    }
    note_failure(state, stamp);
    stats->missing++;
    if (cover_index_find(&state->index, state->key, &position) &&
        !cover_index_retire(&state->index, position, &slot)) {
        return SCAN_WRITE_FAILED;
    }
    return SCAN_OK;
}

static ScanResult place_cover(CoverState *state, const CoverJob *job, const ArtFile *art,
                              CoverStats *stats)
{
    state->expected.kind = ART_EXPECTED;
    state->expected.stamp = art->stamp;
    if (cover_work_failed(state, art->stamp)) {
        stats->missing++;
        return SCAN_OK;
    }
    if (cover_work_reuse(state, stats)) {
        return SCAN_OK;
    }
    Placement placement = decode_cover(state, job, art);
    if (placement == PLACEMENT_UNDECODABLE) {
        return drop_undecodable(state, art->stamp, stats);
    }
    if (placement == PLACEMENT_FAILED) {
        return SCAN_WRITE_FAILED;
    }
    stats->overflow += placement == PLACEMENT_NO_ROOM ? 1u : 0u;
    stats->decoded += placement == PLACEMENT_DONE ? 1u : 0u;
    if (placement == PLACEMENT_DONE) {
        forget_failure(state);
    }
    return SCAN_OK;
}

bool cover_work_keep(CoverState *state)
{
    uint32_t position = 0;
    if (!cover_index_find(&state->index, state->key, &position)) {
        return false;
    }
    cover_index_claim(&state->index, position);
    return true;
}

ScanResult cover_work_decode(CoverState *state, const CoverJob *job, CoverStats *stats)
{
    ArtFile art;
    if (!open_expected(state, job, &state->expected, &art) && !open_art(state, job, &art)) {
        stats->missing++;
        return SCAN_OK;
    }
    guard_art(state, &art);
    ScanResult result = place_cover(state, job, &art, stats);
    job->files.close_file(job->files.context);
    return result;
}

static void probe_art(CoverState *state, const CoverJob *job)
{
    ArtFile art;
    state->expected.kind = ART_ABSENT;
    if (open_art(state, job, &art)) {
        state->expected =
            (ExpectedArt){.kind = ART_EXPECTED, .embedded = false, .stamp = art.stamp};
        job->files.close_file(job->files.context);
    }
}

void cover_work_expect(CoverState *state, const CoverJob *job)
{
    state->expected = expect_art(state);
    if (state->expected.kind == ART_UNKNOWN) {
        probe_art(state, job);
    }
}

bool cover_work_reuse(CoverState *state, CoverStats *stats)
{
    uint32_t position = 0;
    if (state->expected.kind != ART_EXPECTED ||
        !cover_index_find(&state->index, state->key, &position) ||
        !cover_index_reusable(&state->index, position, state->expected.stamp)) {
        return false;
    }
    cover_index_claim(&state->index, position);
    stats->reused++;
    return true;
}

bool cover_work_failed(const CoverState *state, uint32_t stamp)
{
    return state->plan->state != NULL && cover_state_failed(state->plan->state, state->key, stamp);
}

bool cover_work_select(CoverState *state, uint32_t album_index)
{
    state->key = library_album_key(&state->library, album_index);
    return library_read_album(&state->library, album_index, &state->album);
}

bool cover_work_placed(const CoverState *state)
{
    uint32_t position = 0;
    return cover_index_find(&state->index, state->key, &position) &&
           cover_index_claimed(&state->index, position);
}

static bool keep_present(CoverState *state)
{
    for (uint32_t album_index = 0; album_index < state->albums; album_index++) {
        uint32_t position = 0;
        if (cover_index_find(&state->index, library_album_key(&state->library, album_index),
                             &position)) {
            cover_index_claim(&state->index, position);
        }
    }
    return cover_index_drop_unclaimed(&state->index);
}

bool cover_work_settle(CoverState *state)
{
    if (state->index.count == 0) {
        return cover_index_clear(&state->index);
    }
    return keep_present(state);
}

bool cover_work_drop(CoverState *state)
{
    return cover_index_drop_unclaimed(&state->index);
}

bool cover_work_cancelled(const CoverState *state)
{
    return state->plan->cancel != NULL && *state->plan->cancel;
}

static uint32_t now_ms(const CoverState *state)
{
    const CoverClock *clock = &state->plan->clock;
    return clock->now_ms != NULL ? clock->now_ms(clock->context) : 0;
}

bool cover_work_budget_left(const CoverState *state)
{
    const CoverBudget *budget = &state->plan->budget;
    bool albums_left = budget->max_albums == 0 || state->attempts < budget->max_albums;
    bool time_left = budget->max_ms == 0 || state->plan->clock.now_ms == NULL ||
                     now_ms(state) - state->started_ms < budget->max_ms;
    return albums_left && time_left && !cover_work_cancelled(state);
}

void cover_work_report(const CoverJob *job, uint32_t done, uint32_t total)
{
    if (job->progress.report != NULL) {
        job->progress.report(job->progress.context, done, total);
    }
}

bool cover_work_pending(const CoverState *state, uint32_t album_index)
{
    return (state->pending[album_index / BITS_PER_BYTE] >> (album_index % BITS_PER_BYTE) & 1u) != 0;
}

void cover_work_set_pending(CoverState *state, uint32_t album_index, bool pending)
{
    uint8_t mask = (uint8_t)(1u << (album_index % BITS_PER_BYTE));
    uint8_t *byte = &state->pending[album_index / BITS_PER_BYTE];
    *byte = (uint8_t)(pending ? *byte | mask : *byte & ~mask);
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

static void begin(CoverState *state, const CoverPlan *plan, const Library *library)
{
    state->library = *library;
    state->plan = plan;
    state->albums = library_count(library, LIBRARY_ALBUMS);
    state->anchor = COVER_WORK_NO_ALBUM;
    state->attempts = 0;
    state->pending_new = 0;
    state->pending_known = 0;
    state->started_ms = now_ms(state);
}

CoverState *cover_work_start(const CoverJob *job, const CoverPlan *plan, CoverStats *stats,
                             ScanResult *result)
{
    Library probe;
    *stats = (CoverStats){0};
    *result = SCAN_NO_MUSIC_ROOT;
    if (library_open(&probe, job->library, job->library_size) != LIBRARY_OK) {
        return NULL;
    }
    stats->albums = library_count(&probe, LIBRARY_ALBUMS);
    CoverLayout layout = cover_layout_for(job->capacity);
    CoverState *state = start(job, stats->albums, layout);
    *result = state == NULL ? SCAN_OUT_OF_MEMORY : SCAN_NO_ROOM;
    if (state == NULL || (layout.slots == 0 && stats->albums > 0)) {
        return NULL;
    }
    begin(state, plan, &probe);
    art_facts_load(&state->facts, job->scratch, job->scratch_size,
                   library_count(&probe, LIBRARY_TRACKS));
    *result = SCAN_OK;
    return state;
}
