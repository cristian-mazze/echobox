#include "cover_filler_play.h"

#include <stddef.h>

#include "hal/audio.h"
#include "hal/clock.h"

static CoverPlayInput read_input(void)
{
    HalAudioStatus status = hal_audio_status();
    HalAudioStats stats = hal_audio_stats();
    return (CoverPlayInput){.playing = status.state == HAL_AUDIO_PLAYING,
                            .sample_rate = status.format.sample_rate,
                            .buffered_ms = stats.buffered_ms,
                            .underruns = stats.underruns + stats.output_underruns};
}

static uint32_t clock_ms(void *context)
{
    (void)context;
    return hal_clock_milliseconds();
}

static bool serve(void *context)
{
    PlaySession *session = context;
    hal_audio_service();
    uint32_t gap_ms = (hal_clock_microseconds() - session->last_service_us) / 1000;
    cover_play_note_gap(session->guard, gap_ms);
    CoverPlayInput input = read_input();
    bool go = cover_play_may_continue(session->guard, &input) &&
              !session->stop.stopped(session->stop.context);
    session->last_service_us = hal_clock_microseconds();
    return go;
}

static bool allow_write(void *context)
{
    const PlaySession *session = context;
    CoverPlayInput input = read_input();
    return cover_play_may_write(session->guard, &input);
}

void cover_filler_play_observe(CoverPlayGuard *guard)
{
    CoverPlayInput input = read_input();
    cover_play_observe(guard, &input);
}

bool cover_filler_play_playing(void)
{
    return read_input().playing;
}

bool cover_filler_play_open(PlaySession *session, CoverPlayGuard *guard)
{
    CoverPlayInput input = read_input();
    session->guard = guard;
    session->yield = cover_yield_make(clock_ms, serve, session);
    session->play = cover_play_make(&session->yield, allow_write, session);
    session->last_service_us = hal_clock_microseconds();
    return cover_play_may_start(guard, &input);
}

void cover_filler_play_close(PlaySession *session)
{
    cover_play_end(session->guard);
}
