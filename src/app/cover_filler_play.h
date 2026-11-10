#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cover_play_policy.h"
#include "image/cover_yield.h"
#include "library/cover_build.h"
#include "library/cover_play.h"

typedef struct PlaySession {
    CoverPlayGuard *guard;
    CoverYield yield;
    CoverPlay play;
    CoverPoll stop;
    uint32_t last_service_us;
} PlaySession;

void cover_filler_play_observe(CoverPlayGuard *guard);
bool cover_filler_play_playing(void);
bool cover_filler_play_open(PlaySession *session, CoverPlayGuard *guard);
void cover_filler_play_close(PlaySession *session);
