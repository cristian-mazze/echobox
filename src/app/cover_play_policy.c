#include "cover_play_policy.h"

static bool at_least_percent(uint32_t value, uint32_t whole, uint32_t percent)
{
    return (uint64_t)value * 100 >= (uint64_t)whole * percent;
}

static bool format_allowed(const CoverPlayInput *input)
{
    return input->sample_rate > 0 && input->sample_rate <= COVER_PLAY_MAX_RATE;
}

void cover_play_guard_init(CoverPlayGuard *guard)
{
    *guard = (CoverPlayGuard){.disabled = false};
}

void cover_play_track_changed(CoverPlayGuard *guard)
{
    guard->full_ms = 0;
}

void cover_play_observe(CoverPlayGuard *guard, const CoverPlayInput *input)
{
    if (!guard->session_open && input->buffered_ms > guard->full_ms) {
        guard->full_ms = input->buffered_ms;
    }
}

bool cover_play_may_start(CoverPlayGuard *guard, const CoverPlayInput *input)
{
    bool ready = !guard->disabled && input->playing && format_allowed(input) &&
                 guard->full_ms > 0 &&
                 at_least_percent(input->buffered_ms, guard->full_ms, COVER_PLAY_HIGH_PERCENT);
    if (ready) {
        guard->session_open = true;
        guard->session_full_ms = guard->full_ms;
        guard->baseline_underruns = input->underruns;
    }
    return ready;
}

bool cover_play_may_continue(CoverPlayGuard *guard, const CoverPlayInput *input)
{
    if (!guard->session_open || guard->disabled) {
        return false;
    }
    if (input->underruns != guard->baseline_underruns) {
        guard->disabled = true;
        return false;
    }
    return input->playing && format_allowed(input) &&
           at_least_percent(input->buffered_ms, guard->session_full_ms, COVER_PLAY_LOW_PERCENT);
}

bool cover_play_may_write(const CoverPlayGuard *guard, const CoverPlayInput *input)
{
    return guard->session_open && !guard->disabled &&
           at_least_percent(input->buffered_ms, guard->session_full_ms, COVER_PLAY_HIGH_PERCENT);
}

void cover_play_note_gap(CoverPlayGuard *guard, uint32_t gap_ms)
{
    guard->disabled = guard->disabled || gap_ms > COVER_PLAY_MAX_GAP_MS;
}

void cover_play_end(CoverPlayGuard *guard)
{
    guard->session_open = false;
}
