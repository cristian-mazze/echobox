#include "cover_card.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "library/covers.h"
#include "library/library.h"

enum {
    MAX_FILES = 16,
    PATH_CAPACITY = 64,
    MEMORY_SIZE = 384 * 1024,
    LIBRARY_CAPACITY = 4 * 1024 * 1024,
    COVERS_CAPACITY = CARD_COVERS_CAPACITY,
    SCRATCH_SIZE = 16 * 1024 * 1024
};

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

static Card card;
static CardBuffer library_file;
static CardBuffer scratch_file;
CardBuffer card_covers;
uint32_t card_covers_capacity = COVERS_CAPACITY;
CutSink card_cut = {.left = -1};
uint32_t card_opens;

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

static void list_child(const char *name, bool directory, const CardFile *file)
{
    for (uint32_t index = 0; index < card.listed; index++) {
        if (strcmp(card.listing[index].name, name) == 0) {
            return;
        }
    }
    ScanEntry *entry = &card.listing[card.listed++];
    entry->directory = directory;
    entry->modified = directory ? 0 : file->modified;
    entry->size = directory ? 0 : file->size;
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
            list_child(name, directory, &card.files[index]);
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
            card_opens++;
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

bool card_buffer_read(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    const CardBuffer *buffer = context;
    if (offset > buffer->capacity || length > buffer->capacity - offset) {
        return false;
    }
    memcpy(bytes, buffer->bytes + offset, length);
    return true;
}

static bool buffer_write(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    CardBuffer *buffer = context;
    if (offset > buffer->capacity || length > buffer->capacity - offset) {
        return false;
    }
    memcpy(buffer->bytes + offset, bytes, length);
    return true;
}

static bool covers_write(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    card_cut.writes++;
    if (card_cut.left == 0) {
        card_cut.failed_at = card_cut.failed ? card_cut.failed_at : offset;
        card_cut.failed = true;
        return false;
    }
    card_cut.left -= card_cut.left > 0 ? 1 : 0;
    return buffer_write(context, offset, bytes, length);
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

void card_add_album(const char *album, Color color)
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

void card_add_bare_album(const char *album)
{
    char path[PATH_CAPACITY];
    snprintf(path, sizeof path, "/%s/01.flac", album);
    add_file(path, card.flac, card.flac_size);
}

void card_add_broken_album(const char *album)
{
    static const uint8_t BROKEN[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13};
    char path[PATH_CAPACITY];
    card_add_bare_album(album);
    snprintf(path, sizeof path, "/%s/cover.png", album);
    add_file(path, BROKEN, sizeof BROKEN);
}

void card_replace_cover(const char *album, Color color)
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

void card_remove_album(const char *album)
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

void card_reset(void)
{
    if (card.flac == NULL) {
        card.flac = read_fixture("tags/untagged.flac", &card.flac_size);
        library_file = (CardBuffer){calloc(LIBRARY_CAPACITY, 1), LIBRARY_CAPACITY};
        card_covers = (CardBuffer){calloc(COVERS_CAPACITY, 1), COVERS_CAPACITY};
        scratch_file = (CardBuffer){calloc(SCRATCH_SIZE, 1), SCRATCH_SIZE};
    }
    card.count = 0;
    card.png_count = 0;
    card_covers_capacity = COVERS_CAPACITY;
    memset(card_covers.bytes, 0, COVERS_CAPACITY);
}

static uint8_t pass_memory[MEMORY_SIZE];

static ScanFileSystem card_files(void)
{
    return (ScanFileSystem){card_open_dir,  card_next_entry, card_close_dir,
                            card_open_file, card_close_file, NULL};
}

void card_scan(void)
{
    uint8_t *memory = pass_memory;
    ScanFileSystem files = card_files();
    ScanStats scan_stats;
    memset(library_file.bytes, 0, LIBRARY_CAPACITY);
    ScanJob scan = {.files = files,
                    .memory = memory,
                    .size = MEMORY_SIZE,
                    .scratch = {card_buffer_read, buffer_write, &scratch_file, SCRATCH_SIZE},
                    .sink = {buffer_write, &library_file},
                    .capacity = LIBRARY_CAPACITY};
    CHECK_EQUAL(SCAN_OK, library_scan(&scan, &scan_stats));
}

CoverJob card_cover_job(bool keep)
{
    card_covers.capacity = card_covers_capacity;
    return (CoverJob){.files = card_files(),
                      .library = {card_buffer_read, &library_file},
                      .library_size = LIBRARY_CAPACITY,
                      .sink = {covers_write, &card_covers},
                      .previous = {keep ? card_buffer_read : NULL, &card_covers},
                      .capacity = card_covers_capacity,
                      .memory = pass_memory,
                      .size = MEMORY_SIZE,
                      .scratch = {card_buffer_read, &scratch_file},
                      .scratch_size = SCRATCH_SIZE};
}

ScanResult card_cover_pass(bool keep, CoverStats *stats)
{
    CoverJob job = card_cover_job(keep);
    return cover_build(&job, stats);
}

ScanResult card_rescan(bool keep, CoverStats *stats)
{
    card_scan();
    return card_cover_pass(keep, stats);
}

uint32_t card_album_key(const char *title)
{
    Library library;
    CHECK_EQUAL(LIBRARY_OK, library_open(&library, (ByteSource){card_buffer_read, &library_file},
                                         LIBRARY_CAPACITY));
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

uint16_t card_cover_pixel(const char *title)
{
    static uint16_t pixels[COVER_FULL_PIXELS];
    Covers covers;
    uint32_t record = 0;
    CHECK(covers_open(&covers, (ByteSource){card_buffer_read, &card_covers}, card_covers_capacity));
    if (!covers_find(&covers, card_album_key(title), &record)) {
        return 0;
    }
    CHECK(covers_read(&covers, record, COVER_FULL, pixels));
    return pixels[COVER_FULL_PIXELS / 2];
}

uint32_t card_album_index(const char *title)
{
    Library library;
    CHECK_EQUAL(LIBRARY_OK, library_open(&library, (ByteSource){card_buffer_read, &library_file},
                                         LIBRARY_CAPACITY));
    for (uint32_t index = 0; index < library_count(&library, LIBRARY_ALBUMS); index++) {
        LibraryAlbum album;
        char text[PATH_CAPACITY];
        CHECK(library_read_album(&library, index, &album));
        CHECK(library_read_string(&library, album.title, text, sizeof text));
        if (strcmp(text, title) == 0) {
            return index;
        }
    }
    CHECK(false);
    return 0;
}
