#include "library_rescan.h"

#include <stddef.h>

#include "core/i18n.h"
#include "core/string_builder.h"
#include "cover_cache.h"
#include "cover_state_store.h"
#include "hal/clock.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "library/cover_state.h"
#include "library/library.h"
#include "library_store.h"

static const char MUSIC_ROOT[] = "/";
const char LIBRARY_SCRATCH_PATH[] = "/.echo/build.tmp";
const char RESCAN_TIMES_PATH[] = "/.echo/rescan_times.bin";

enum {
    NUMBER_CAPACITY = 12,
    STATE_ROOM = (COVER_STATE_BYTES + 7) & ~7
};

enum {
    TIME_LIST,
    TIME_OPEN,
    TIME_READ,
    TIME_WRITE,
    TIME_TOTAL,
    TIME_KINDS,
    RESCAN_PASSES = 2,
    TIME_SLOTS = RESCAN_PASSES * TIME_KINDS
};

enum {
    TIMES_VERSION = 2,
    TIMES_VERSION_FIELD = 4,
    TIMES_SPENT_FIELD = 8,
    TIMES_COVERS_FIELD = TIMES_SPENT_FIELD + TIME_SLOTS * 2 * 4,
    TIMES_COVER_COUNTERS = 7,
    TIMES_BYTES = TIMES_COVERS_FIELD + TIMES_COVER_COUNTERS * 4
};

static uint32_t spent[TIME_SLOTS * 2];
static uint32_t pass;

static void spend(uint32_t kind, uint32_t since)
{
    uint32_t slot = pass * TIME_KINDS + kind;
    spent[slot] += hal_clock_microseconds() - since;
    spent[TIME_SLOTS + slot]++;
}

typedef struct RescanFiles {
    HalDir dir;
    HalFile file;
    HalFile library;
    HalFile covers;
    HalFile scratch;
    HalDirEntry entry;
    RescanProgress progress;
    RescanAnchor anchor;
} RescanFiles;

static bool open_dir(void *context, const char *path)
{
    RescanFiles *files = context;
    uint32_t since = hal_clock_microseconds();
    bool opened = hal_dir_open(&files->dir, path);
    spend(TIME_LIST, since);
    return opened;
}

static bool next_entry(void *context, ScanEntry *entry)
{
    RescanFiles *files = context;
    hal_system_keep_alive();
    uint32_t since = hal_clock_microseconds();
    bool listed = hal_dir_next(&files->dir, &files->entry);
    spend(TIME_LIST, since);
    if (!listed) {
        return false;
    }
    entry->directory = files->entry.directory;
    entry->modified = files->entry.modified;
    entry->size = files->entry.size;
    for (size_t index = 0; index < sizeof entry->name; index++) {
        entry->name[index] = files->entry.name[index];
    }
    return true;
}

static void close_dir(void *context)
{
    RescanFiles *files = context;
    hal_dir_close(&files->dir);
}

static bool read_handle(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    const HalFile *file = context;
    hal_system_keep_alive();
    uint32_t since = hal_clock_microseconds();
    bool read = hal_file_read(*file, offset, buffer, length);
    spend(TIME_READ, since);
    return read;
}

static bool open_file(void *context, const char *path, ByteSource *source, uint32_t *size)
{
    RescanFiles *files = context;
    uint32_t since = hal_clock_microseconds();
    bool opened = hal_file_open(&files->file, path);
    spend(TIME_OPEN, since);
    if (!opened) {
        return false;
    }
    *source = (ByteSource){.read = read_handle, .context = &files->file};
    *size = files->file.size;
    return true;
}

static void close_file(void *context)
{
    RescanFiles *files = context;
    hal_file_close(&files->file);
}

static bool timed_write(const char *path, uint32_t offset, const void *bytes, uint32_t length)
{
    uint32_t since = hal_clock_microseconds();
    bool written = hal_file_write_at(path, offset, bytes, length);
    spend(TIME_WRITE, since);
    return written;
}

static bool write_library(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    (void)context;
    hal_system_keep_alive();
    return timed_write(LIBRARY_STORE_PATH, offset, bytes, length);
}

static bool write_scratch(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    (void)context;
    hal_system_keep_alive();
    return timed_write(LIBRARY_SCRATCH_PATH, offset, bytes, length);
}

static void report(void *context, uint32_t files, uint32_t tracks)
{
    RescanFiles *rescan = context;
    hal_system_keep_alive();
    if (rescan->progress.tracks != NULL) {
        rescan->progress.tracks(files, tracks);
    }
}

static bool write_covers(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    (void)context;
    hal_system_keep_alive();
    return timed_write(COVER_CACHE_PATH, offset, bytes, length);
}

static void report_covers(void *context, uint32_t done, uint32_t total)
{
    RescanFiles *rescan = context;
    hal_system_keep_alive();
    if (rescan->progress.covers != NULL) {
        rescan->progress.covers(done, total);
    }
}

static uint32_t reserved_size_of(const char *path)
{
    HalFile file;
    if (!hal_file_open(&file, path)) {
        return 0;
    }
    uint32_t size = file.size;
    hal_file_close(&file);
    return size;
}

static bool music_root_exists(void)
{
    HalDir dir;
    if (!hal_dir_open(&dir, MUSIC_ROOT)) {
        return false;
    }
    hal_dir_close(&dir);
    return true;
}

static uint32_t rescan_clock(void *context)
{
    (void)context;
    return hal_clock_milliseconds();
}

static uint32_t fingerprint_of(RescanFiles *files)
{
    Library library;
    ByteSource source = {read_handle, &files->library};
    bool opened = library_open(&library, source, files->library.size) == LIBRARY_OK;
    return opened ? library_fingerprint(&library) : 0;
}

static CoverJob cover_job(const ScanJob *scan, uint32_t capacity, bool previous)
{
    RescanFiles *files = scan->files.context;
    return (CoverJob){
        .files = scan->files,
        .library = {read_handle, &files->library},
        .library_size = files->library.size,
        .sink = {write_covers, NULL},
        .previous = {previous ? read_handle : NULL, &files->covers},
        .capacity = capacity,
        .memory = (uint8_t *)scan->memory + STATE_ROOM,
        .size = scan->size - STATE_ROOM,
        .progress = {report_covers, files},
        .scratch = {read_handle, &files->scratch},
        .scratch_size = files->scratch.size,
    };
}

static ScanResult reconcile_covers(const LibraryScanEntry *entry, const ScanJob *scan,
                                   uint32_t capacity, CoverStats *stats)
{
    RescanFiles *files = scan->files.context;
    uint8_t *state = scan->memory;
    cover_state_store_load(state, fingerprint_of(files));
    bool previous = hal_file_open(&files->covers, COVER_CACHE_PATH);
    CoverJob job = cover_job(scan, capacity, previous);
    CoverPlan plan = {.budget = {COVER_INITIAL_MS, COVER_INITIAL_ALBUMS},
                      .clock = {rescan_clock, NULL},
                      .anchored = files->anchor.known,
                      .anchor_key = files->anchor.key,
                      .state = state};
    ScanResult result = entry->reconcile(&job, &plan, stats);
    (void)cover_state_store_save(state);
    if (previous) {
        hal_file_close(&files->covers);
    }
    return result;
}

static ScanResult build_covers(const LibraryScanEntry *entry, const ScanJob *scan,
                               CoverStats *stats)
{
    RescanFiles *files = scan->files.context;
    uint32_t capacity = reserved_size_of(COVER_CACHE_PATH);
    *stats = (CoverStats){0};
    if (capacity == 0 || scan->size <= STATE_ROOM) {
        return SCAN_NO_ROOM;
    }
    if (!hal_file_open(&files->library, LIBRARY_STORE_PATH)) {
        return SCAN_WRITE_FAILED;
    }
    ScanResult result = reconcile_covers(entry, scan, capacity, stats);
    hal_file_close(&files->library);
    return result;
}

static void put_times_u32(uint8_t *bytes, uint32_t value)
{
    for (uint32_t index = 0; index < 4; index++) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

static void pack_cover_counters(const CoverStats *covers, uint8_t *bytes)
{
    const uint32_t counters[TIMES_COVER_COUNTERS] = {
        covers->albums,  covers->covers,   covers->reused, covers->missing,
        covers->decoded, covers->overflow, covers->pending};
    for (uint32_t index = 0; index < TIMES_COVER_COUNTERS; index++) {
        put_times_u32(bytes + index * 4, counters[index]);
    }
}

static void save_times(const CoverStats *covers)
{
    uint8_t bytes[TIMES_BYTES] = {'E', 'M', 'R', 'T'};
    bytes[TIMES_VERSION_FIELD] = TIMES_VERSION;
    for (uint32_t slot = 0; slot < TIME_SLOTS * 2; slot++) {
        put_times_u32(bytes + TIMES_SPENT_FIELD + slot * 4, spent[slot]);
    }
    pack_cover_counters(covers, bytes + TIMES_COVERS_FIELD);
    (void)hal_file_write(RESCAN_TIMES_PATH, bytes, sizeof bytes);
}

static void run_passes(const LibraryScanEntry *entry, const ScanJob *job, RescanOutcome *outcome)
{
    for (uint32_t slot = 0; slot < TIME_SLOTS * 2; slot++) {
        spent[slot] = 0;
    }
    pass = 0;
    uint32_t since = hal_clock_microseconds();
    outcome->result = job->memory != NULL ? entry->scan(job, &outcome->stats) : SCAN_NO_SCRATCH;
    spend(TIME_TOTAL, since);
    if (outcome->result == SCAN_OK) {
        pass = 1;
        since = hal_clock_microseconds();
        outcome->cover_result = build_covers(entry, job, &outcome->covers);
        spend(TIME_TOTAL, since);
    }
    save_times(&outcome->covers);
}

static RescanFiles *rescan_files(void)
{
    static RescanFiles files;
    return &files;
}

RescanOutcome library_rescan_run(RescanProgress progress, RescanAnchor anchor)
{
    RescanFiles *files = rescan_files();
    RescanOutcome outcome = {.result = SCAN_NO_MUSIC_ROOT, .cover_result = SCAN_OK};
    if (hal_system_restart_pending()) {
        outcome.result = SCAN_RESTART_NEEDED;
        return outcome;
    }
    uint32_t capacity = reserved_size_of(LIBRARY_STORE_PATH);
    if (!music_root_exists()) {
        return outcome;
    }
    if (capacity == 0 || !hal_file_open(&files->scratch, LIBRARY_SCRATCH_PATH)) {
        outcome.result = SCAN_NO_ROOM;
        return outcome;
    }
    const LibraryScanEntry *entry = hal_system_load_rescan_code();
    if (entry == NULL || entry->magic != LIBRARY_SCAN_MAGIC) {
        hal_file_close(&files->scratch);
        outcome.result = SCAN_NO_CODE;
        return outcome;
    }
    files->progress = progress;
    files->anchor = anchor;
    uint32_t size = 0;
    void *memory = hal_system_borrow_scratch(&size);
    uint32_t listing_size = 0;
    void *listing = memory != NULL ? hal_system_borrow_listing(&listing_size) : NULL;
    ScanJob job = {
        .files = {open_dir, next_entry, close_dir, open_file, close_file, files},
        .memory = memory,
        .size = size,
        .listing = listing,
        .listing_size = listing_size,
        .scratch = {read_handle, write_scratch, &files->scratch, files->scratch.size},
        .sink = {write_library, NULL},
        .capacity = capacity,
        .progress = {report, files},
    };
    run_passes(entry, &job, &outcome);
    hal_file_close(&files->scratch);
    hal_system_return_scratch();
    return outcome;
}

static uint32_t albums_of(RescanFiles *files)
{
    Library library;
    ByteSource source = {read_handle, &files->library};
    bool opened = library_open(&library, source, files->library.size) == LIBRARY_OK;
    return opened ? library_count(&library, LIBRARY_ALBUMS) : 0;
}

static void exchange_queue(uint8_t *state, const CoverFillRequest *request)
{
    if (request->queue.keys != NULL) {
        cover_state_set_queue(state, request->queue.keys, request->queue.count);
        return;
    }
    uint32_t count = cover_state_queue_count(state);
    for (uint32_t index = 0; index < count && request->restored.keys != NULL; index++) {
        request->restored.keys[index] = cover_state_queue_key(state, index);
    }
    if (request->restored.count != NULL) {
        *request->restored.count = request->restored.keys != NULL ? count : 0;
    }
}

static void prepare_state(RescanFiles *files, uint8_t *state, const CoverFillRequest *request)
{
    cover_state_store_load(state, fingerprint_of(files));
    exchange_queue(state, request);
    if (request->restart_when_done && cover_state_cursor(state) >= albums_of(files)) {
        cover_state_set_cursor(state, 0);
    }
}

static ScanResult fill_in(const LibraryScanEntry *entry, RescanFiles *files,
                          const CoverFillRequest *request, CoverStats *stats)
{
    uint32_t size = 0;
    uint8_t *memory = hal_system_borrow_scratch(&size);
    if (memory == NULL || size <= STATE_ROOM) {
        hal_system_return_scratch();
        return SCAN_NO_SCRATCH;
    }
    prepare_state(files, memory, request);
    bool previous = hal_file_open(&files->covers, COVER_CACHE_PATH);
    ScanJob scan = {.files = {open_dir, next_entry, close_dir, open_file, close_file, files},
                    .memory = memory,
                    .size = size};
    CoverJob job = cover_job(&scan, request->capacity, previous);
    CoverPlan plan = {.budget = request->budget,
                      .clock = {rescan_clock, NULL},
                      .priority = request->priority,
                      .priority_count = request->priority_count,
                      .state = memory,
                      .poll = request->poll};
    ScanResult result = entry->fill(&job, &plan, stats);
    (void)cover_state_store_save(memory);
    if (previous) {
        hal_file_close(&files->covers);
    }
    hal_system_return_scratch();
    return result;
}

static ScanResult fill_with_code(RescanFiles *files, const CoverFillRequest *request,
                                 CoverStats *stats)
{
    const LibraryScanEntry *entry = hal_system_load_rescan_code();
    if (entry == NULL || entry->magic != LIBRARY_SCAN_MAGIC) {
        return SCAN_NO_CODE;
    }
    return fill_in(entry, files, request, stats);
}

CoverFillOutcome library_rescan_fill(const CoverFillRequest *request)
{
    RescanFiles *files = rescan_files();
    CoverFillOutcome outcome = {.result = SCAN_RESTART_NEEDED, .stats = {0}};
    if (hal_system_restart_pending()) {
        return outcome;
    }
    outcome.result = SCAN_NO_ROOM;
    if (request->capacity == 0 || !hal_file_open(&files->library, LIBRARY_STORE_PATH)) {
        return outcome;
    }
    bool facts = hal_file_open(&files->scratch, LIBRARY_SCRATCH_PATH);
    files->scratch.size = facts ? files->scratch.size : 0;
    files->progress = (RescanProgress){NULL, request->progress};
    outcome.result = fill_with_code(files, request, &outcome.stats);
    if (facts) {
        hal_file_close(&files->scratch);
    }
    hal_file_close(&files->library);
    return outcome;
}

uint32_t library_rescan_covers_capacity(void)
{
    return reserved_size_of(COVER_CACHE_PATH);
}

static const char *error_text(ScanResult result)
{
    switch (result) {
    case SCAN_OK:
        return "";
    case SCAN_NO_MUSIC_ROOT:
        return i18n_text(STR_RESCAN_NO_MUSIC);
    case SCAN_OUT_OF_MEMORY:
        return i18n_text(STR_RESCAN_OUT_OF_MEMORY);
    case SCAN_TOO_MANY:
        return i18n_text(STR_RESCAN_TOO_MANY);
    case SCAN_NO_SCRATCH:
        return i18n_text(STR_RESCAN_NO_SCRATCH);
    case SCAN_FOLDER_TOO_BIG:
        return i18n_text(STR_RESCAN_FOLDER_TOO_BIG);
    case SCAN_PATH_TOO_LONG:
        return i18n_text(STR_RESCAN_PATH_TOO_LONG);
    case SCAN_WRITE_FAILED:
        return i18n_text(STR_RESCAN_WRITE_FAILED);
    case SCAN_NO_ROOM:
        return i18n_text(STR_RESCAN_NO_ROOM);
    case SCAN_NO_CODE:
        return i18n_text(STR_RESCAN_NO_CODE);
    case SCAN_RESTART_NEEDED:
        return i18n_text(STR_RESCAN_RESTART_NEEDED);
    case SCAN_CANCELLED:
        return "";
    }
    return "";
}

static void number_text(char *text, size_t capacity, uint32_t value)
{
    StringBuilder builder = string_builder_make(text, capacity);
    string_builder_append_unsigned(&builder, value);
}

static StringId library_pattern(RescanOutcome outcome)
{
    static const StringId PATTERNS[2][4] = {
        {STR_RESCAN_LIBRARY_TRACKS, STR_RESCAN_LIBRARY_TRACKS_NO_COVERS,
         STR_RESCAN_LIBRARY_TRACKS_COVER, STR_RESCAN_LIBRARY_TRACKS_COVERS},
        {STR_RESCAN_LIBRARY_TRACK, STR_RESCAN_LIBRARY_TRACK_NO_COVERS,
         STR_RESCAN_LIBRARY_TRACK_COVER, STR_RESCAN_LIBRARY_TRACK_COVERS},
    };
    size_t kind = 0;
    if (outcome.cover_result != SCAN_OK) {
        kind = 1;
    } else if (outcome.covers.albums > 0) {
        kind = outcome.covers.covers == 1 ? 2 : 3;
    }
    return PATTERNS[outcome.stats.tracks == 1 ? 1 : 0][kind];
}

const char *library_rescan_message(RescanOutcome outcome, char *text, uint32_t capacity)
{
    if (outcome.result != SCAN_OK) {
        return error_text(outcome.result);
    }
    char tracks[NUMBER_CAPACITY];
    char covers[NUMBER_CAPACITY];
    number_text(tracks, sizeof tracks, outcome.stats.tracks);
    number_text(covers, sizeof covers, outcome.covers.covers);
    const char *arguments[] = {tracks, covers};
    StringBuilder builder = string_builder_make(text, capacity);
    i18n_format(&builder, library_pattern(outcome), arguments, 2);
    return text;
}

const char *library_rescan_fill_message(CoverFillOutcome outcome, char *text, uint32_t capacity)
{
    if (outcome.result == SCAN_CANCELLED) {
        return i18n_text(STR_COVERS_SCAN_STOPPED);
    }
    if (outcome.result != SCAN_OK) {
        return error_text(outcome.result);
    }
    char decoded[NUMBER_CAPACITY];
    number_text(decoded, sizeof decoded, outcome.stats.decoded);
    const char *arguments[] = {decoded};
    StringBuilder builder = string_builder_make(text, capacity);
    i18n_format(&builder, STR_COVERS_SCAN_DONE, arguments, 1);
    return text;
}
