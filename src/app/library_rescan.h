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
} RescanProgress;

extern const char LIBRARY_SCRATCH_PATH[];
extern const char RESCAN_TIMES_PATH[];

RescanOutcome library_rescan_run(RescanProgress progress, RescanAnchor anchor);
const char *library_rescan_message(RescanOutcome outcome, char *text, uint32_t capacity);
