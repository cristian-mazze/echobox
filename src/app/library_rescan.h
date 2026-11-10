#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/cover_build.h"
#include "library/library_scan.h"

typedef struct RescanOutcome {
    ScanResult result;
    ScanStats stats;
    ScanResult cover_result;
    CoverStats covers;
} RescanOutcome;

typedef struct RescanAnchor {
    bool known;
    uint32_t key;
} RescanAnchor;

typedef struct RescanProgress {
    void (*tracks)(uint32_t files, uint32_t tracks);
    void (*covers)(uint32_t done, uint32_t total);
    void (*cover_batch)(uint32_t decoding, uint32_t later);
} RescanProgress;

typedef struct CoverBatch {
    uint32_t decoding;
    uint32_t later;
} CoverBatch;

typedef struct CoverQueueKeys {
    const uint32_t *keys;
    uint32_t count;
} CoverQueueKeys;

typedef struct CoverRestoredKeys {
    uint32_t *keys;
    uint32_t *count;
} CoverRestoredKeys;

struct CoverPlay;

typedef struct CoverFillRequest {
    const uint16_t *priority;
    uint32_t priority_count;
    CoverBudget budget;
    CoverPoll poll;
    CoverQueueKeys queue;
    CoverRestoredKeys restored;
    bool restart_when_done;
    uint32_t capacity;
    void (*progress)(uint32_t done, uint32_t total);
    const struct CoverPlay *play;
} CoverFillRequest;

typedef struct CoverFillOutcome {
    ScanResult result;
    CoverStats stats;
} CoverFillOutcome;

extern const char LIBRARY_SCRATCH_PATH[];
extern const char RESCAN_TIMES_PATH[];

RescanOutcome library_rescan_run(RescanProgress progress, RescanAnchor anchor);
const char *library_rescan_message(RescanOutcome outcome, char *text, uint32_t capacity);
const char *library_rescan_covers_message(RescanOutcome outcome, bool manual, char *text,
                                          uint32_t capacity);
const char *library_rescan_batch_detail(CoverBatch batch, bool manual, char *text,
                                        uint32_t capacity);
CoverFillOutcome library_rescan_fill(const CoverFillRequest *request);
const char *library_rescan_fill_message(CoverFillOutcome outcome, char *text, uint32_t capacity);
uint32_t library_rescan_covers_capacity(void);
