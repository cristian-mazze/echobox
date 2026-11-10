#include "cover_filler_play.h"

void cover_filler_play_observe(CoverPlayGuard *guard)
{
    (void)guard;
}

bool cover_filler_play_playing(void)
{
    return false;
}

bool cover_filler_play_open(PlaySession *session, CoverPlayGuard *guard)
{
    (void)session;
    (void)guard;
    return false;
}

void cover_filler_play_close(PlaySession *session)
{
    (void)session;
}
