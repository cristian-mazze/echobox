#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/power_policy.h"
#include "cover_cache.h"
#include "cover_queue.h"
#include "library/library.h"
#include "library_rescan.h"
#include "player.h"

enum {
    COVER_FILLER_POLL_US = 20000
};

typedef struct CoverFiller {
    CoverQueue queue;
    uint32_t quiet_since_ms;
    bool complete;
    bool queue_restored;
    bool has_playing_album;
    uint16_t playing_album;
    bool has_seen_track;
    uint16_t seen_track;
    uint32_t polled_us;
    bool stop;
    uint32_t sessions;
    uint32_t pending;
    bool stalled;
} CoverFiller;

typedef struct CoverFillerLinks {
    Player *player;
    CoverCache *covers;
    const Library *library;
    const PowerPolicy *power;
    bool (*input_arrived)(void *context);
    void *context;
} CoverFillerLinks;

typedef struct CoverFillerWorld {
    bool enabled;
    bool display_on;
    bool rescan_waiting;
} CoverFillerWorld;

void cover_filler_start(CoverFiller *filler, uint32_t now_ms);
void cover_filler_follow(CoverFiller *filler, const CoverFillerLinks *links, uint32_t now_ms);
bool cover_filler_due(const CoverFiller *filler, CoverFillerWorld world, uint32_t now_ms);
bool cover_filler_syncing(const CoverFiller *filler);
CoverFillOutcome cover_filler_run_idle(CoverFiller *filler, const CoverFillerLinks *links);
CoverFillOutcome cover_filler_run_all(CoverFiller *filler, const CoverFillerLinks *links,
                                      void (*progress)(uint32_t done, uint32_t total));
