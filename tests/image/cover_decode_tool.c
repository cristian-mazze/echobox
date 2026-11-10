#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "image/cover_image.h"

enum {
    SCRATCH_BYTES = 384 * 1024,
    RESULT_EXIT_BASE = 10
};

typedef struct Memory {
    uint8_t *bytes;
    uint32_t size;
} Memory;

static bool read_memory(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    const Memory *memory = context;
    if (offset > memory->size || length > memory->size - offset) {
        return false;
    }
    memcpy(buffer, memory->bytes + offset, length);
    return true;
}

static bool load(const char *path, Memory *memory)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    memory->bytes = malloc((size_t)size + 1);
    memory->size = (uint32_t)size;
    bool read =
        memory->bytes != NULL && fread(memory->bytes, 1, (size_t)size, file) == (size_t)size;
    fclose(file);
    return read;
}

typedef struct Rows {
    uint16_t *full;
    uint16_t *half;
    uint32_t full_count;
    uint32_t half_count;
} Rows;

static bool take_full(void *context, uint32_t y, const uint16_t *pixels)
{
    Rows *rows = context;
    if (y != rows->full_count || y >= COVER_GRID_SIZE) {
        return false;
    }
    memcpy(rows->full + y * COVER_GRID_SIZE, pixels, COVER_GRID_SIZE * sizeof pixels[0]);
    rows->full_count++;
    return true;
}

static bool take_half(void *context, uint32_t y, const uint16_t *pixels)
{
    Rows *rows = context;
    if (y != rows->half_count || y >= COVER_GRID_HALF) {
        return false;
    }
    memcpy(rows->half + y * COVER_GRID_HALF, pixels, COVER_GRID_HALF * sizeof pixels[0]);
    rows->half_count++;
    return true;
}

typedef struct Pacing {
    uint32_t slice;
    uint32_t cancel_at;
    bool wall_clock;
    uint32_t ticks;
    uint32_t services;
} Pacing;

static Pacing pacing;

static uint32_t tick_now(void *context)
{
    (void)context;
    return pacing.ticks++;
}

static uint32_t wall_now(void *context)
{
    (void)context;
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (uint32_t)((uint64_t)now.tv_sec * 1000000u + (uint64_t)now.tv_nsec / 1000u);
}

static bool count_service(void *context)
{
    (void)context;
    pacing.services++;
    return pacing.cancel_at == 0 || pacing.services < pacing.cancel_at;
}

static uint32_t gap_us;

static CoverImageResult decode_rolling(Memory *memory, uint16_t *full, uint16_t *half)
{
    static uint8_t scratch[SCRATCH_BYTES];
    Rows rows = {full, half, 0, 0};
    CoverYield yield =
        cover_yield_make(pacing.wall_clock ? wall_now : tick_now, count_service, NULL);
    yield.slice_ms = pacing.slice;
    CoverRowsRequest request = {.source = {read_memory, memory, 0, memory->size},
                                .memory = {scratch, sizeof scratch},
                                .sink = {take_full, take_half, &rows},
                                .yield = pacing.slice > 0 ? &yield : NULL};
    pacing.ticks = 0;
    pacing.services = 0;
    CoverImageResult result = cover_image_decode_rows(&request);
    gap_us = yield.longest_ms;
    bool complete = rows.full_count == COVER_GRID_SIZE && rows.half_count == COVER_GRID_HALF;
    return result == COVER_IMAGE_OK && !complete ? COVER_IMAGE_CORRUPT : result;
}

static CoverImageResult decode_full(Memory *memory, uint16_t *full, uint16_t *half)
{
    static uint8_t scratch[SCRATCH_BYTES];
    ImageSource source = {read_memory, memory, 0, memory->size};
    return cover_image_decode(source, scratch, sizeof scratch, full, half);
}

static bool rolling_mode;

static CoverImageResult decode(Memory *memory, uint16_t *full, uint16_t *half)
{
    return rolling_mode ? decode_rolling(memory, full, half) : decode_full(memory, full, half);
}

static bool write_record(const char *path, const uint16_t *full, const uint16_t *half)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    bool written = fwrite(full, 2, COVER_GRID_CELLS, file) == COVER_GRID_CELLS &&
                   fwrite(half, 2, COVER_GRID_HALF_CELLS, file) == COVER_GRID_HALF_CELLS;
    return fclose(file) == 0 && written;
}

static uint32_t next_random(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state >> 8;
}

static void mutate(Memory *target, const Memory *original, uint32_t *state)
{
    memcpy(target->bytes, original->bytes, original->size);
    target->size = original->size;
    uint32_t kind = next_random(state) % 3;
    if (kind == 0) {
        target->size = next_random(state) % (original->size + 1);
        return;
    }
    uint32_t changes = 1 + next_random(state) % (kind == 1 ? 4 : 64);
    for (uint32_t change = 0; change < changes && original->size > 0; change++) {
        target->bytes[next_random(state) % original->size] = (uint8_t)next_random(state);
    }
}

static int report_gaps(const char *path)
{
    static uint16_t full[COVER_GRID_CELLS];
    static uint16_t half[COVER_GRID_HALF_CELLS];
    Memory memory;
    if (!load(path, &memory)) {
        return 1;
    }
    pacing.slice = 1;
    pacing.wall_clock = true;
    CoverImageResult result = decode(&memory, full, half);
    printf("%s: result %d, %u yields, longest stretch %u us\n", path, (int)result, pacing.services,
           gap_us);
    free(memory.bytes);
    return 0;
}

static int fuzz(const char *path, uint32_t rounds, uint32_t seed)
{
    static uint16_t full[COVER_GRID_CELLS];
    static uint16_t half[COVER_GRID_HALF_CELLS];
    Memory original;
    if (!load(path, &original)) {
        fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }
    Memory mutated = {malloc(original.size + 1), original.size};
    uint32_t state = seed;
    uint32_t decoded = 0;
    for (uint32_t round = 0; round < rounds; round++) {
        mutate(&mutated, &original, &state);
        decoded += decode(&mutated, full, half) == COVER_IMAGE_OK;
    }
    printf("%s: %u of %u mutations still decoded\n", path, decoded, rounds);
    free(mutated.bytes);
    free(original.bytes);
    return 0;
}

int main(int argc, char **argv)
{
    static uint16_t full[COVER_GRID_CELLS];
    static uint16_t half[COVER_GRID_HALF_CELLS];
    if (argc > 1 && strcmp(argv[1], "--rolling") == 0) {
        rolling_mode = true;
        argc--;
        argv++;
    }
    if (argc == 6 && rolling_mode && strcmp(argv[1], "--yield") == 0) {
        pacing.slice = (uint32_t)atoi(argv[2]);
        pacing.cancel_at = (uint32_t)atoi(argv[3]);
        argc -= 3;
        argv += 3;
    }
    if (argc == 3 && rolling_mode && strcmp(argv[1], "--gaps") == 0) {
        return report_gaps(argv[2]);
    }
    if (argc == 5 && strcmp(argv[1], "--fuzz") == 0) {
        return fuzz(argv[2], (uint32_t)atoi(argv[3]), (uint32_t)atoi(argv[4]));
    }
    Memory memory;
    if (argc != 3 || !load(argv[1], &memory)) {
        fprintf(stderr, "usage: cover_decode_tool [--rolling] IMAGE RECORD | [--rolling] --fuzz "
                        "IMAGE ROUNDS SEED\n");
        return 1;
    }
    CoverImageResult result = decode(&memory, full, half);
    free(memory.bytes);
    if (result != COVER_IMAGE_OK) {
        return RESULT_EXIT_BASE + (int)result;
    }
    return write_record(argv[2], full, half) ? 0 : 1;
}
