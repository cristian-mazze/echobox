#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "cover_png.h"
#include "library/cover_build.h"
#include "library/covers.h"
#include "library/library_scan.h"

enum {
    ALBUMS = 300,
    MEMORY_SIZE = 384 * 1024,
    LIBRARY_CAPACITY = 4 * 1024 * 1024,
    SCRATCH_SIZE = 16 * 1024 * 1024,
    RECORD_SLOT = COVERS_ENTRY_SIZE + 4 + COVER_BUILD_RECORD_BYTES,
    COVERS_CAPACITY = COVERS_HEADER_SIZE + ALBUMS * RECORD_SLOT,
    FLAC_INFO_END = 4 + 4 + 34 + 4 + 14,
    PICTURE_FIELDS = 32,
    PICTURE_MIME_LENGTH = 9,
    FLAC_PICTURE_BLOCK = 6,
    FRONT_COVER = 3,
    ROOT = -1,
    NAME_CAPACITY = 32
};

typedef struct Blob {
    uint8_t *bytes;
    uint32_t size;
} Blob;

typedef struct Buffer {
    uint8_t *bytes;
    uint32_t capacity;
} Buffer;

typedef struct LargeCard {
    Blob plain;
    Blob embedded;
    Blob png;
    int open_folder;
    uint32_t cursor;
    uint32_t dirs_opened;
    uint32_t files_opened;
} LargeCard;

static LargeCard card;
static Buffer library_file;
static Buffer covers_file;
static Buffer scratch_file;

static bool has_embedded_art(uint32_t album)
{
    return album % 2 == 0;
}

static bool parse_album(const char *path, uint32_t *album, const char **rest)
{
    unsigned value = 0;
    int used = 0;
    if (sscanf(path, "/A%3u%n", &value, &used) != 1 || used != 5 || value >= ALBUMS) {
        return false;
    }
    *album = value;
    *rest = path + used;
    return true;
}

static bool card_open_dir(void *context, const char *path)
{
    (void)context;
    uint32_t album = 0;
    const char *rest = NULL;
    card.dirs_opened++;
    card.cursor = 0;
    if (strcmp(path, "/") == 0) {
        card.open_folder = ROOT;
        return true;
    }
    if (!parse_album(path, &album, &rest) || (rest[0] != '\0' && strcmp(rest, "/") != 0)) {
        return false;
    }
    card.open_folder = (int)album;
    return true;
}

static uint32_t folder_entries(void)
{
    if (card.open_folder == ROOT) {
        return ALBUMS;
    }
    return has_embedded_art((uint32_t)card.open_folder) ? 1 : 2;
}

static void name_entry(ScanEntry *entry)
{
    entry->directory = card.open_folder == ROOT;
    entry->modified = entry->directory ? 0 : 1000;
    if (entry->directory) {
        snprintf(entry->name, sizeof entry->name, "A%03u", card.cursor);
        return;
    }
    snprintf(entry->name, sizeof entry->name, "%s", card.cursor == 0 ? "01.flac" : "cover.png");
}

static bool card_next_entry(void *context, ScanEntry *entry)
{
    (void)context;
    if (card.cursor == folder_entries()) {
        return false;
    }
    name_entry(entry);
    card.cursor++;
    return true;
}

static void card_close_dir(void *context)
{
    (void)context;
}

static bool blob_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    const Blob *blob = context;
    if (offset > blob->size || length > blob->size - offset) {
        return false;
    }
    memcpy(buffer, blob->bytes + offset, length);
    return true;
}

static Blob *file_at(const char *path)
{
    uint32_t album = 0;
    const char *rest = NULL;
    if (!parse_album(path, &album, &rest)) {
        return NULL;
    }
    if (strcmp(rest, "/01.flac") == 0) {
        return has_embedded_art(album) ? &card.embedded : &card.plain;
    }
    bool folder_art = !has_embedded_art(album) && strcmp(rest, "/cover.png") == 0;
    return folder_art ? &card.png : NULL;
}

static bool card_open_file(void *context, const char *path, ByteSource *source, uint32_t *size)
{
    (void)context;
    Blob *blob = file_at(path);
    card.files_opened++;
    if (blob == NULL) {
        return false;
    }
    *source = (ByteSource){.read = blob_read, .context = blob};
    *size = blob->size;
    return true;
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

static Blob read_fixture(const char *name)
{
    char path[512];
    Blob blob = {NULL, 0};
    snprintf(path, sizeof path, "%s/%s", ECHO_FIXTURE_DIR, name);
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL);
    fseek(file, 0, SEEK_END);
    blob.size = (uint32_t)ftell(file);
    fseek(file, 0, SEEK_SET);
    blob.bytes = malloc(blob.size);
    CHECK(fread(blob.bytes, 1, blob.size, file) == blob.size);
    fclose(file);
    return blob;
}

static uint8_t *put_be32(uint8_t *at, uint32_t value)
{
    at[0] = (uint8_t)(value >> 24);
    at[1] = (uint8_t)(value >> 16);
    at[2] = (uint8_t)(value >> 8);
    at[3] = (uint8_t)value;
    return at + 4;
}

static uint8_t *put_picture_block(uint8_t *at, const Blob *image)
{
    uint32_t length = PICTURE_FIELDS + PICTURE_MIME_LENGTH + image->size;
    at = put_be32(at, (uint32_t)FLAC_PICTURE_BLOCK << 24 | length);
    at = put_be32(at, FRONT_COVER);
    at = put_be32(at, PICTURE_MIME_LENGTH);
    memcpy(at, "image/png", PICTURE_MIME_LENGTH);
    at = put_be32(at + PICTURE_MIME_LENGTH, 0);
    memset(at, 0, 16);
    at = put_be32(at + 16, image->size);
    memcpy(at, image->bytes, image->size);
    return at + image->size;
}

static Blob embed_picture(const Blob *flac, const Blob *image)
{
    Blob blob = {malloc(flac->size + PICTURE_FIELDS + PICTURE_MIME_LENGTH + 4 + image->size), 0};
    CHECK(memcmp(flac->bytes, "fLaC", 4) == 0);
    memcpy(blob.bytes, flac->bytes, FLAC_INFO_END);
    uint8_t *at = put_picture_block(blob.bytes + FLAC_INFO_END, image);
    memcpy(at, flac->bytes + FLAC_INFO_END, flac->size - FLAC_INFO_END);
    blob.size = (uint32_t)(at - blob.bytes) + flac->size - FLAC_INFO_END;
    return blob;
}

static void prepare_card(void)
{
    static const Color ORANGE = {255, 128, 0, 0xFC00};
    if (card.plain.bytes != NULL) {
        return;
    }
    card.plain = read_fixture("tags/untagged.flac");
    card.png = (Blob){malloc(COVER_PNG_CAPACITY), 0};
    card.png.size = cover_png_make(card.png.bytes, ORANGE);
    card.embedded = embed_picture(&card.plain, &card.png);
    library_file = (Buffer){calloc(LIBRARY_CAPACITY, 1), LIBRARY_CAPACITY};
    covers_file = (Buffer){calloc(COVERS_CAPACITY, 1), COVERS_CAPACITY};
    scratch_file = (Buffer){calloc(SCRATCH_SIZE, 1), SCRATCH_SIZE};
}

static ScanFileSystem card_files(void)
{
    return (ScanFileSystem){card_open_dir,  card_next_entry, card_close_dir,
                            card_open_file, card_close_file, NULL};
}

static void scan_library(void *memory)
{
    ScanStats stats;
    memset(library_file.bytes, 0, LIBRARY_CAPACITY);
    ScanJob scan = {.files = card_files(),
                    .memory = memory,
                    .size = MEMORY_SIZE,
                    .scratch = {buffer_read, buffer_write, &scratch_file, SCRATCH_SIZE},
                    .sink = {buffer_write, &library_file},
                    .capacity = LIBRARY_CAPACITY};
    CHECK_EQUAL(SCAN_OK, library_scan(&scan, &stats));
    CHECK_EQUAL(ALBUMS, stats.tracks);
}

static ScanResult rescan(CoverStats *stats)
{
    static uint8_t memory[MEMORY_SIZE];
    scan_library(memory);
    card.dirs_opened = 0;
    card.files_opened = 0;
    CoverJob job = {.files = card_files(),
                    .library = {buffer_read, &library_file},
                    .library_size = LIBRARY_CAPACITY,
                    .sink = {buffer_write, &covers_file},
                    .previous = {buffer_read, &covers_file},
                    .capacity = COVERS_CAPACITY,
                    .memory = memory,
                    .size = sizeof memory};
    return cover_build(&job, stats);
}

static void report_pass(const char *name, const CoverStats *stats)
{
    printf("%s: %u albums, %u covers, %u reused, %u missing, %u decoded, %u files opened, "
           "%u folders listed\n",
           name, stats->albums, stats->covers, stats->reused, stats->missing, stats->decoded,
           card.files_opened, card.dirs_opened);
}

static void the_first_rescan_decodes_every_album(void)
{
    CoverStats stats;
    prepare_card();
    memset(covers_file.bytes, 0, COVERS_CAPACITY);
    CHECK_EQUAL(SCAN_OK, rescan(&stats));
    report_pass("first rescan", &stats);
    CHECK_EQUAL(ALBUMS, stats.albums);
    CHECK_EQUAL(ALBUMS, stats.covers);
    CHECK_EQUAL(ALBUMS, stats.decoded);
    CHECK_EQUAL(0u, stats.reused);
    CHECK_EQUAL(0u, stats.missing);
}

static void an_unchanged_rescan_decodes_nothing(void)
{
    CoverStats stats;
    prepare_card();
    memset(covers_file.bytes, 0, COVERS_CAPACITY);
    CHECK_EQUAL(SCAN_OK, rescan(&stats));
    CHECK_EQUAL(SCAN_OK, rescan(&stats));
    report_pass("unchanged rescan", &stats);
    CHECK_EQUAL(ALBUMS, stats.covers);
    CHECK_EQUAL(ALBUMS, stats.reused);
    CHECK_EQUAL(0u, stats.decoded);
    CHECK_EQUAL(0u, stats.missing);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_first_rescan_decodes_every_album),
        TEST_CASE(an_unchanged_rescan_decodes_nothing),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
