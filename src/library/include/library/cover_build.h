#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/byte_source.h"
#include "library/library_builder.h"
#include "library/library_scan.h"

enum {
    COVER_BUILD_RECORD_BYTES = (112 * 112 + 56 * 56) * 2,
    COVER_BUILD_MAX_COVERS = 0xFFFF,
    COVER_INITIAL_MS = 60000,
    COVER_INITIAL_ALBUMS = 100
};

typedef struct CoverProgress {
    void (*report)(void *context, uint32_t done, uint32_t total);
    void *context;
} CoverProgress;

typedef struct CoverJob {
    ScanFileSystem files;
    ByteSource library;
    uint32_t library_size;
    ByteSink sink;
    ByteSource previous;
    uint32_t capacity;
    void *memory;
    uint32_t size;
    CoverProgress progress;
    ByteSource scratch;
    uint32_t scratch_size;
} CoverJob;

typedef struct CoverClock {
    uint32_t (*now_ms)(void *context);
    void *context;
} CoverClock;

typedef struct CoverBudget {
    uint32_t max_ms;
    uint32_t max_albums;
} CoverBudget;

typedef struct CoverPoll {
    bool (*stopped)(void *context);
    void *context;
} CoverPoll;

typedef struct CoverPlan {
    CoverBudget budget;
    CoverClock clock;
    const uint16_t *priority;
    uint32_t priority_count;
    bool anchored;
    uint32_t anchor_key;
    uint8_t *state;
    const bool *cancel;
    CoverPoll poll;
} CoverPlan;

typedef struct CoverStats {
    uint32_t albums;
    uint32_t covers;
    uint32_t reused;
    uint32_t missing;
    uint32_t decoded;
    uint32_t overflow;
    uint32_t pending;
} CoverStats;

ScanResult cover_reconcile(const CoverJob *job, const CoverPlan *plan, CoverStats *stats);
ScanResult cover_fill(const CoverJob *job, const CoverPlan *plan, CoverStats *stats);
ScanResult cover_build(const CoverJob *job, CoverStats *stats);
