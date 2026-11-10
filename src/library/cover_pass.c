#include "cover_work.h"
#include "library/cover_build.h"
#include "library/cover_flow_order.h"
#include "library/cover_state.h"

static bool album_at(const CoverState *state, CoverFlowOrder order, uint32_t position,
                     uint32_t *album)
{
    uint16_t linked = 0;
    if (position < order.count) {
        bool found = cover_flow_order_album(&state->library, order, position, &linked);
        *album = linked;
        return found && linked < state->albums;
    }
    *album = position;
    return position < state->albums;
}

static bool around(uint32_t start, uint32_t step, uint32_t total, uint32_t *position)
{
    uint32_t distance = (step + 1) / 2;
    if (step % 2 == 1) {
        *position = start + distance;
        return *position < total;
    }
    *position = start - distance;
    return distance <= start;
}

static void compare_album(CoverState *state, const CoverJob *job, CoverStats *stats,
                          uint32_t album_index)
{
    if (!cover_work_select(state, album_index) || cover_work_placed(state)) {
        return;
    }
    if (state->plan->anchored && state->key == state->plan->anchor_key) {
        state->anchor = album_index;
    }
    cover_work_expect(state, job);
    if (state->expected.kind != ART_EXPECTED || cover_work_failed(state, state->expected.stamp)) {
        stats->missing++;
        return;
    }
    if (cover_work_reuse(state, stats)) {
        return;
    }
    bool known = cover_work_keep(state);
    state->pending_known += known ? 1u : 0u;
    state->pending_new += known ? 0u : 1u;
    cover_work_set_pending(state, album_index, true);
}

static void compare_albums(CoverState *state, const CoverJob *job, CoverStats *stats)
{
    for (uint32_t album_index = 0; album_index < state->albums; album_index++) {
        cover_work_report(job, album_index, state->albums);
        compare_album(state, job, stats, album_index);
    }
}

static ScanResult decode_pending_album(CoverState *state, const CoverJob *job, CoverStats *stats,
                                       uint32_t album_index)
{
    if (!cover_work_select(state, album_index)) {
        return SCAN_OK;
    }
    bool known = cover_work_keep(state);
    cover_work_expect(state, job);
    ScanResult result = cover_work_decode(state, job, stats);
    if (result == SCAN_CANCELLED) {
        return result;
    }
    cover_work_set_pending(state, album_index, false);
    state->pending_known -= known ? 1u : 0u;
    state->pending_new -= known ? 0u : 1u;
    return result;
}

static uint32_t start_position(const CoverState *state, CoverFlowOrder order)
{
    uint32_t position = 0;
    if (state->anchor == COVER_WORK_NO_ALBUM ||
        !cover_flow_order_find(&state->library, order, (uint16_t)state->anchor, &position)) {
        return state->anchor < state->albums && state->anchor >= order.count ? state->anchor : 0;
    }
    return position;
}

static uint32_t overflow_of(const CoverState *state)
{
    uint32_t free_slots = state->index.layout.slots - state->index.count;
    return state->pending_new > free_slots ? state->pending_new - free_slots : 0;
}

static uint32_t announce_batch(const CoverState *state, const CoverJob *job)
{
    uint32_t wanted = state->pending_new + state->pending_known - overflow_of(state);
    uint32_t limit = state->plan->budget.max_albums;
    uint32_t decoding = limit != 0 && wanted > limit ? limit : wanted;
    if (job->progress.batch != NULL) {
        job->progress.batch(job->progress.context, decoding, wanted - decoding);
    }
    return decoding;
}

static uint32_t decoded_so_far(const CoverState *state, uint32_t wanted)
{
    return wanted - state->pending_new - state->pending_known;
}

static bool more_to_decode(const CoverState *state, uint32_t step)
{
    return step < 2 * state->albums + 1 && state->pending_new + state->pending_known > 0 &&
           cover_work_budget_left(state);
}

static ScanResult decode_pending(CoverState *state, const CoverJob *job, CoverStats *stats)
{
    CoverFlowOrder order = cover_flow_order_make(&state->library);
    uint32_t start = start_position(state, order);
    uint32_t wanted = state->pending_new + state->pending_known;
    uint32_t decoding = announce_batch(state, job);
    ScanResult result = SCAN_OK;
    for (uint32_t step = 0; result == SCAN_OK && more_to_decode(state, step); step++) {
        uint32_t position = 0;
        uint32_t album = 0;
        if (around(start, step, state->albums, &position) &&
            album_at(state, order, position, &album) && cover_work_pending(state, album)) {
            cover_work_report(job, decoded_so_far(state, wanted), decoding);
            result = decode_pending_album(state, job, stats, album);
        }
    }
    cover_work_report(job, decoded_so_far(state, wanted), decoding);
    return result;
}

static void count_left(const CoverState *state, CoverStats *stats)
{
    uint32_t over = overflow_of(state);
    stats->covers = state->index.count;
    stats->overflow += over;
    stats->pending = state->pending_new + state->pending_known - over;
}

static bool counts_settled(ScanResult result)
{
    return result == SCAN_OK || result == SCAN_CANCELLED;
}

static void note_reconciled(const CoverState *state, ScanResult result, const CoverStats *stats)
{
    uint8_t *saved = state->plan->state;
    if (saved != NULL) {
        cover_state_set_pending(saved, counts_settled(result) ? stats->pending
                                                              : COVER_STATE_PENDING_UNKNOWN);
    }
}

ScanResult cover_reconcile(const CoverJob *job, const CoverPlan *plan, CoverStats *stats)
{
    ScanResult result = SCAN_OK;
    CoverState *state = cover_work_start(job, plan, stats, &result);
    if (state == NULL) {
        return result;
    }
    if (!cover_work_settle(state)) {
        return SCAN_WRITE_FAILED;
    }
    compare_albums(state, job, stats);
    if (!cover_work_drop(state)) {
        return SCAN_WRITE_FAILED;
    }
    result = decode_pending(state, job, stats);
    count_left(state, stats);
    note_reconciled(state, result, stats);
    return result;
}

static ScanResult fill_album(CoverState *state, const CoverJob *job, CoverStats *stats,
                             uint32_t album_index)
{
    if (album_index >= state->albums || !cover_work_select(state, album_index)) {
        return SCAN_OK;
    }
    cover_work_expect(state, job);
    if (state->expected.kind != ART_EXPECTED || cover_work_failed(state, state->expected.stamp) ||
        cover_work_reuse(state, stats)) {
        return SCAN_OK;
    }
    return cover_work_decode(state, job, stats);
}

static ScanResult unless_cancelled(const CoverState *state, ScanResult result)
{
    return result == SCAN_OK && cover_work_cancelled(state) ? SCAN_CANCELLED : result;
}

static ScanResult fill_priority(CoverState *state, const CoverJob *job, CoverStats *stats)
{
    ScanResult result = SCAN_OK;
    for (uint32_t index = 0;
         result == SCAN_OK && index < state->plan->priority_count && cover_work_budget_left(state);
         index++) {
        result = fill_album(state, job, stats, state->plan->priority[index]);
    }
    return unless_cancelled(state, result);
}

static ScanResult fill_from_cursor(CoverState *state, const CoverJob *job, CoverStats *stats)
{
    CoverFlowOrder order = cover_flow_order_make(&state->library);
    uint8_t *saved = state->plan->state;
    uint32_t position = saved != NULL ? cover_state_cursor(saved) : 0;
    ScanResult result = SCAN_OK;
    while (result == SCAN_OK && position < state->albums && cover_work_budget_left(state)) {
        uint32_t album = 0;
        cover_work_report(job, position, state->albums);
        result = album_at(state, order, position, &album) ? fill_album(state, job, stats, album)
                                                          : SCAN_OK;
        result = unless_cancelled(state, result);
        position += result == SCAN_OK ? 1u : 0u;
    }
    if (saved != NULL) {
        cover_state_set_cursor(saved, position);
    }
    stats->pending = state->albums - position;
    return result;
}

static uint32_t pending_after(const CoverState *state, const uint8_t *saved, ScanResult result)
{
    uint32_t before = cover_state_pending(saved);
    if (!counts_settled(result)) {
        return COVER_STATE_PENDING_UNKNOWN;
    }
    if (cover_state_cursor(saved) >= state->albums) {
        return 0;
    }
    if (before == COVER_STATE_PENDING_UNKNOWN) {
        return before;
    }
    return before > state->attempts ? before - state->attempts : 0;
}

static void note_filled(const CoverState *state, ScanResult result, CoverStats *stats)
{
    uint8_t *saved = state->plan->state;
    if (saved == NULL) {
        return;
    }
    uint32_t left = pending_after(state, saved, result);
    uint32_t cursor = cover_state_cursor(saved);
    cover_state_set_pending(saved, left);
    bool unknown = left == COVER_STATE_PENDING_UNKNOWN;
    stats->pending = unknown && cursor < state->albums ? state->albums - cursor : left;
}

ScanResult cover_fill(const CoverJob *job, const CoverPlan *plan, CoverStats *stats)
{
    ScanResult result = SCAN_OK;
    CoverState *state = cover_work_start(job, plan, stats, &result);
    if (state == NULL) {
        return result;
    }
    if (state->index.count == 0 && !cover_index_clear(&state->index)) {
        return SCAN_WRITE_FAILED;
    }
    result = fill_priority(state, job, stats);
    if (result == SCAN_OK) {
        result = fill_from_cursor(state, job, stats);
    }
    note_filled(state, result, stats);
    stats->covers = state->index.count;
    return result;
}

ScanResult cover_build(const CoverJob *job, CoverStats *stats)
{
    static const CoverPlan everything = {.budget = {0, 0}, .anchored = false};
    return cover_reconcile(job, &everything, stats);
}
