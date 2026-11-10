#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    COVER_PLAY_MAX_RATE = 48000,
    COVER_PLAY_HIGH_PERCENT = 85,
    COVER_PLAY_LOW_PERCENT = 50,
    COVER_PLAY_MAX_GAP_MS = 10
};

typedef struct CoverPlayInput {
    bool playing;
    uint32_t sample_rate;
    uint32_t buffered_ms;
    uint32_t underruns;
} CoverPlayInput;

typedef struct CoverPlayGuard {
    bool disabled;
    bool session_open;
    uint32_t full_ms;
    uint32_t session_full_ms;
    uint32_t baseline_underruns;
} CoverPlayGuard;

void cover_play_guard_init(CoverPlayGuard *guard);
void cover_play_track_changed(CoverPlayGuard *guard);
void cover_play_observe(CoverPlayGuard *guard, const CoverPlayInput *input);
bool cover_play_may_start(CoverPlayGuard *guard, const CoverPlayInput *input);
bool cover_play_may_continue(CoverPlayGuard *guard, const CoverPlayInput *input);
bool cover_play_may_write(const CoverPlayGuard *guard, const CoverPlayInput *input);
void cover_play_note_gap(CoverPlayGuard *guard, uint32_t gap_ms);
void cover_play_end(CoverPlayGuard *guard);
