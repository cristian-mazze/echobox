#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "cover_png.h"
#include "library/cover_build.h"
#include "library/covers.h"
#include "library/library.h"
#include "library/library_scan.h"

enum {
    MAX_FILES = 16,
    PATH_CAPACITY = 64,
    MEMORY_SIZE = 384 * 1024,
    LIBRARY_CAPACITY = 4 * 1024 * 1024,
    COVERS_CAPACITY = 1024 * 1024,
    SCRATCH_SIZE = 16 * 1024 * 1024,
    RECORD_SLOT = 8 + 4 + COVER_BUILD_RECORD_BYTES
};

static const Color RED = {255, 0, 0, 0xF800};
static const Color GREEN = {0, 255, 0, 0x07E0};
static const Color BLUE = {0, 0, 255, 0x001F};
static const Color WHITE = {255, 255, 255, 0xFFFF};

typedef struct CardFile {
    char path[PATH_CAPACITY];
    const uint8_t *bytes;
    uint32_t size;
    uint32_t modified;
} CardFile;

typedef struct Card {
    CardFile files[MAX_FILES];
    uint32_t count;
    ScanEntry listing[MAX_FILES];
    uint32_t listed;
    uint32_t cursor;
    uint8_t pngs[MAX_FILES][COVER_PNG_CAPACITY];
    uint32_t png_count;
    uint8_t *flac;
    uint32_t flac_size;
} Card;

typedef struct Buffer {
    uint8_t *bytes;
    uint32_t capacity;
} Buffer;

static Card card;
static Buffer library_file;
static Buffer covers_file;
static Buffer scratch_file;
static uint32_t covers_capacity = COVERS_CAPACITY;

static bool child_of(const char *dir, const char *path, char *name, bool *directory)
{
    size_t length = strlen(dir);
    while (length > 0 && dir[length - 1] == '/') {
        length--;
    }
    size_t prefix = length + 1;
    if (strncmp(path, dir, length) != 0 || path[length] != '/') {
        return false;
    }
    const char *rest = path + prefix;
    const char *slash = strchr(rest, '/');
    size_t name_length = slash != NULL ? (size_t)(slash - rest) : strlen(rest);
    memcpy(name, rest, name_length);
    name[name_length] = '\0';
    *directory = slash != NULL;
    return name_length > 0;
}

static void list_child(const char *name, bool directory, uint32_t modified)
{
    for (uint32_t index = 0; index < card.listed; index++) {
        if (strcmp(card.listing[index].name, name) == 0) {
            return;
        }
    }
    ScanEntry *entry = &card.listing[card.listed++];
    entry->directory = directory;
    entry->modified = directory ? 0 : modified;
    snprintf(entry->name, sizeof entry->name, "%s", name);
}

static bool card_open_dir(void *context, const char *path)
{
    (void)context;
    card.listed = 0;
    card.cursor = 0;
    for (uint32_t index = 0; index < card.count; index++) {
        char name[PATH_CAPACITY];
        bool directory = false;
        if (child_of(path, card.files[index].path, name, &directory)) {
            list_child(name, directory, card.files[index].modified);
        }
    }
    return card.listed > 0;
}

static bool card_next_entry(void *context, ScanEntry *entry)
{
    (void)context;
    if (card.cursor == card.listed) {
        return false;
    }
    *entry = card.listing[card.cursor++];
    return true;
}

static void card_close_dir(void *context)
{
    (void)context;
}

static bool card_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    const CardFile *file = context;
    if (offset > file->size || length > file->size - offset) {
        return false;
    }
    memcpy(buffer, file->bytes + offset, length);
    return true;
}

static bool card_open_file(void *context, const char *path, ByteSource *source, uint32_t *size)
{
    (void)context;
    for (uint32_t index = 0; index < card.count; index++) {
        if (strcmp(card.files[index].path, path) == 0) {
            *source = (ByteSource){.read = card_read, .context = &card.files[index]};
            *size = card.files[index].size;
            return true;
        }
    }
    return false;
}

static void card_close_file(void *context)
{
    (void)context;
}

static bool buffer_read(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    const Buffer *buffer = context;
    if (offset > buffer->capacity || length > buffer->capacity - offset) {
        return false;
    }
    memcpy(bytes, buffer->bytes + offset, length);
    return true;
}

static bool buffer_write(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    Buffer *buffer = context;
    if (offset > buffer->capacity || length > buffer->capacity - offset) {
        return false;
    }
    memcpy(buffer->bytes + offset, bytes, length);
    return true;
}

static uint8_t *read_fixture(const char *name, uint32_t *size)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", ECHO_FIXTURE_DIR, name);
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL);
    fseek(file, 0, SEEK_END);
    *size = (uint32_t)ftell(file);
    fseek(file, 0, SEEK_SET);
    uint8_t *bytes = malloc(*size);
    CHECK(fread(bytes, 1, *size, file) == *size);
    fclose(file);
    return bytes;
}

static void add_file(const char *path, const uint8_t *bytes, uint32_t size)
{
    CardFile *file = &card.files[card.count++];
    snprintf(file->path, sizeof file->path, "%s", path);
    file->bytes = bytes;
    file->size = size;
    file->modified = 1000;
}

static void add_album(const char *album, Color color)
{
    char path[PATH_CAPACITY];
    uint8_t *png = card.pngs[card.png_count++];
    snprintf(path, sizeof path, "/%s/01.flac", album);
    add_file(path, card.flac, card.flac_size);
    snprintf(path, sizeof path, "/%s/cover.png", album);
    add_file(path, png, cover_png_make(png, color));
}

static CardFile *find_file(const char *path)
{
    for (uint32_t index = 0; index < card.count; index++) {
        if (strcmp(card.files[index].path, path) == 0) {
            return &card.files[index];
        }
    }
    return NULL;
}

static void replace_cover(const char *album, Color color)
{
    char path[PATH_CAPACITY];
    snprintf(path, sizeof path, "/%s/cover.png", album);
    CardFile *file = find_file(path);
    CHECK(file != NULL);
    uint8_t *png = card.pngs[card.png_count++];
    file->size = cover_png_make(png, color);
    file->bytes = png;
    file->modified++;
}

static void remove_album(const char *album)
{
    char prefix[PATH_CAPACITY];
    int length = snprintf(prefix, sizeof prefix, "/%s/", album);
    uint32_t kept = 0;
    for (uint32_t index = 0; index < card.count; index++) {
        if (strncmp(card.files[index].path, prefix, (size_t)length) != 0) {
            card.files[kept++] = card.files[index];
        }
    }
    card.count = kept;
}

static void reset(void)
{
    if (card.flac == NULL) {
        card.flac = read_fixture("tags/untagged.flac", &card.flac_size);
        library_file = (Buffer){calloc(LIBRARY_CAPACITY, 1), LIBRARY_CAPACITY};
        covers_file = (Buffer){calloc(COVERS_CAPACITY, 1), COVERS_CAPACITY};
        scratch_file = (Buffer){calloc(SCRATCH_SIZE, 1), SCRATCH_SIZE};
    }
    card.count = 0;
    card.png_count = 0;
    covers_capacity = COVERS_CAPACITY;
    memset(covers_file.bytes, 0, COVERS_CAPACITY);
}

static ScanResult rescan(bool keep, CoverStats *stats)
{
    static uint8_t memory[MEMORY_SIZE];
    ScanFileSystem files = {card_open_dir,  card_next_entry, card_close_dir,
                            card_open_file, card_close_file, NULL};
    ScanStats scan_stats;
    memset(library_file.bytes, 0, LIBRARY_CAPACITY);
    ScanJob scan = {.files = files,
                    .memory = memory,
                    .size = sizeof memory,
                    .scratch = {buffer_read, buffer_write, &scratch_file, SCRATCH_SIZE},
                    .sink = {buffer_write, &library_file},
                    .capacity = LIBRARY_CAPACITY};
    CHECK_EQUAL(SCAN_OK, library_scan(&scan, &scan_stats));
    covers_file.capacity = covers_capacity;
    CoverJob job = {.files = files,
                    .library = {buffer_read, &library_file},
                    .library_size = LIBRARY_CAPACITY,
                    .sink = {buffer_write, &covers_file},
                    .previous = {keep ? buffer_read : NULL, &covers_file},
                    .capacity = covers_capacity,
                    .memory = memory,
                    .size = sizeof memory};
    return cover_build(&job, stats);
}

static uint32_t album_key(const char *title)
{
    Library library;
    CHECK_EQUAL(LIBRARY_OK,
                library_open(&library, (ByteSource){buffer_read, &library_file}, LIBRARY_CAPACITY));
    for (uint32_t index = 0; index < library_count(&library, LIBRARY_ALBUMS); index++) {
        LibraryAlbum album;
        char text[PATH_CAPACITY];
        CHECK(library_read_album(&library, index, &album));
        CHECK(library_read_string(&library, album.title, text, sizeof text));
        if (strcmp(text, title) == 0) {
            return library_album_key(&library, index);
        }
    }
    CHECK(false);
    return 0;
}

static uint16_t cover_pixel(const char *title)
{
    static uint16_t pixels[COVER_FULL_PIXELS];
    Covers covers;
    uint32_t record = 0;
    CHECK(covers_open(&covers, (ByteSource){buffer_read, &covers_file}, covers_capacity));
    if (!covers_find(&covers, album_key(title), &record)) {
        return 0;
    }
    CHECK(covers_read(&covers, record, COVER_FULL, pixels));
    return pixels[COVER_FULL_PIXELS / 2];
}

static void three_albums(void)
{
    reset();
    add_album("Alpha", RED);
    add_album("Beta", GREEN);
    add_album("Gamma", BLUE);
}

static void the_first_build_decodes_every_album(void)
{
    CoverStats stats;
    three_albums();
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(3u, stats.albums);
    CHECK_EQUAL(3u, stats.covers);
    CHECK_EQUAL(0u, stats.reused);
    CHECK_EQUAL(RED.pixel, cover_pixel("Alpha"));
    CHECK_EQUAL(GREEN.pixel, cover_pixel("Beta"));
    CHECK_EQUAL(BLUE.pixel, cover_pixel("Gamma"));
}

static void an_unchanged_card_reuses_every_cover(void)
{
    CoverStats stats;
    three_albums();
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(3u, stats.covers);
    CHECK_EQUAL(3u, stats.reused);
    CHECK_EQUAL(0u, stats.missing);
    CHECK_EQUAL(RED.pixel, cover_pixel("Alpha"));
    CHECK_EQUAL(GREEN.pixel, cover_pixel("Beta"));
    CHECK_EQUAL(BLUE.pixel, cover_pixel("Gamma"));
}

static void a_new_album_is_the_only_one_decoded(void)
{
    CoverStats stats;
    three_albums();
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    add_album("Delta", WHITE);
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(4u, stats.covers);
    CHECK_EQUAL(3u, stats.reused);
    CHECK_EQUAL(WHITE.pixel, cover_pixel("Delta"));
    CHECK_EQUAL(BLUE.pixel, cover_pixel("Gamma"));
}

static void replaced_art_is_decoded_again(void)
{
    CoverStats stats;
    three_albums();
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    replace_cover("Beta", WHITE);
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(3u, stats.covers);
    CHECK_EQUAL(2u, stats.reused);
    CHECK_EQUAL(WHITE.pixel, cover_pixel("Beta"));
    CHECK_EQUAL(RED.pixel, cover_pixel("Alpha"));
}

static void a_removed_album_frees_its_place_for_a_new_one(void)
{
    CoverStats stats;
    three_albums();
    covers_capacity = COVERS_HEADER_SIZE + 3 * RECORD_SLOT;
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(3u, stats.covers);
    remove_album("Beta");
    add_album("Delta", WHITE);
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(3u, stats.covers);
    CHECK_EQUAL(2u, stats.reused);
    CHECK_EQUAL(0u, stats.missing);
    CHECK_EQUAL(WHITE.pixel, cover_pixel("Delta"));
    CHECK_EQUAL(RED.pixel, cover_pixel("Alpha"));
    CHECK_EQUAL(BLUE.pixel, cover_pixel("Gamma"));
}

static void an_album_without_art_loses_its_cover(void)
{
    CoverStats stats;
    three_albums();
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    remove_album("Beta");
    add_file("/Beta/01.flac", card.flac, card.flac_size);
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(2u, stats.covers);
    CHECK_EQUAL(1u, stats.missing);
    CHECK_EQUAL(0u, cover_pixel("Beta"));
}

static void a_file_without_stamps_is_built_again(void)
{
    CoverStats stats;
    three_albums();
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    memset(covers_file.bytes + 12, 0, 4);
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(3u, stats.covers);
    CHECK_EQUAL(0u, stats.reused);
    CHECK_EQUAL(GREEN.pixel, cover_pixel("Beta"));
}

static void a_damaged_index_is_ignored(void)
{
    CoverStats stats;
    uint8_t first[8];
    three_albums();
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    memcpy(first, covers_file.bytes + COVERS_HEADER_SIZE, sizeof first);
    memcpy(covers_file.bytes + COVERS_HEADER_SIZE, covers_file.bytes + COVERS_HEADER_SIZE + 8, 8);
    memcpy(covers_file.bytes + COVERS_HEADER_SIZE + 8, first, sizeof first);
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(3u, stats.covers);
    CHECK_EQUAL(0u, stats.reused);
    CHECK_EQUAL(RED.pixel, cover_pixel("Alpha"));
    CHECK_EQUAL(GREEN.pixel, cover_pixel("Beta"));
}

static void without_a_previous_file_everything_is_decoded(void)
{
    CoverStats stats;
    three_albums();
    CHECK_EQUAL(SCAN_OK, rescan(true, &stats));
    CHECK_EQUAL(SCAN_OK, rescan(false, &stats));
    CHECK_EQUAL(3u, stats.covers);
    CHECK_EQUAL(0u, stats.reused);
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
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
