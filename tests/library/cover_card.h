#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cover_png.h"
#include "library/cover_build.h"
#include "library/library_scan.h"

enum {
    CARD_RECORD_SLOT = 8 + 4 + COVER_BUILD_RECORD_BYTES,
    CARD_COVERS_CAPACITY = 1024 * 1024
};

typedef struct CardBuffer {
    uint8_t *bytes;
    uint32_t capacity;
} CardBuffer;

typedef struct CutSink {
    int32_t left;
    uint32_t writes;
    bool failed;
    uint32_t failed_at;
} CutSink;

extern CardBuffer card_covers;
extern uint32_t card_covers_capacity;
extern CutSink card_cut;
extern uint32_t card_opens;

bool card_buffer_read(void *context, uint32_t offset, void *bytes, uint32_t length);
void card_reset(void);
void card_add_album(const char *album, Color color);
void card_add_bare_album(const char *album);
void card_add_broken_album(const char *album);
void card_replace_cover(const char *album, Color color);
void card_remove_album(const char *album);
void card_scan(void);
CoverJob card_cover_job(bool keep);
ScanResult card_cover_pass(bool keep, CoverStats *stats);
ScanResult card_rescan(bool keep, CoverStats *stats);
uint32_t card_album_key(const char *title);
uint32_t card_album_index(const char *title);
uint16_t card_cover_pixel(const char *title);
