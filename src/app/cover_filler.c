#include "cover_filler.h"

#include <stddef.h>

#include "cover_fill_policy.h"
#include "hal/clock.h"
#include "hal/input.h"
#include "hal/power.h"
#include "hal/system.h"

enum {
    NO_ALBUM = 0xFFFF
};

typedef bool (*StopCheck)(const CoverFillerLinks *links, bool slow_checks);

typedef struct FillMode {
    CoverBudget budget;
    StopCheck check;
    bool manual;
    void (*progress)(uint32_t done, uint32_t total);
} FillMode;

typedef struct FillSession {
    CoverFiller *filler;
    const CoverFillerLinks *links;
    StopCheck check;
} FillSession;

void cover_filler_start(CoverFiller *filler, uint32_t now_ms)
{
    cover_queue_init(&filler->queue);
    filler->quiet_since_ms = now_ms;
    filler->complete = false;
    filler->queue_restored = false;
    filler->has_playing_album = false;
    filler->has_seen_track = false;
    filler->polled_us = 0;
    filler->stop = false;
    filler->sessions = 0;
}

static void follow_playing(CoverFiller *filler, const CoverFillerLinks *links, uint32_t now_ms)
{
    PlayerStatus status = player_status(links->player);
    LibraryTrack track;
    if (status.state == PLAYER_PLAYING) {
        filler->quiet_since_ms = now_ms;
    }
    if (!status.has_track || (filler->has_seen_track && filler->seen_track == status.track)) {
        return;
    }
    filler->has_seen_track = true;
    filler->seen_track = status.track;
    if (library_read_track(links->library, status.track, &track)) {
        filler->has_playing_album = true;
        filler->playing_album = track.album;
        filler->complete = filler->complete && cover_cache_has(links->covers, track.album);
    }
}

static void follow_misses(CoverFiller *filler, const CoverFillerLinks *links)
{
    uint16_t album = 0;
    if (cover_cache_take_missed(links->covers, &album)) {
        cover_queue_push(&filler->queue, album, library_album_key(links->library, album));
        filler->complete = false;
    }
}

void cover_filler_follow(CoverFiller *filler, const CoverFillerLinks *links, uint32_t now_ms)
{
    follow_playing(filler, links, now_ms);
    follow_misses(filler, links);
}

bool cover_filler_due(const CoverFiller *filler, CoverFillerWorld world, uint32_t now_ms)
{
    if (filler->complete || world.display_on || world.rescan_waiting ||
        now_ms - filler->quiet_since_ms < COVER_FILL_QUIET_MS) {
        return false;
    }
    HalBattery battery = hal_power_battery();
    CoverFillConditions conditions = {.playing = false,
                                      .quiet_since_ms = filler->quiet_since_ms,
                                      .display_on = world.display_on,
                                      .usb_power = battery.usb_power,
                                      .battery_known = battery.known,
                                      .battery_percent = battery.percent,
                                      .restart_pending = hal_system_restart_pending(),
                                      .rescan_waiting = world.rescan_waiting,
                                      .work_left = !filler->complete};
    return cover_fill_ready(&conditions, now_ms);
}

static bool power_changed(const CoverFillerLinks *links)
{
    return hal_power_battery().usb_power ||
           power_policy_shutdown_due(links->power, hal_clock_milliseconds());
}

static bool idle_interrupted(const CoverFillerLinks *links, bool slow_checks)
{
    bool keys = links->input_arrived(links->context);
    return keys || (slow_checks && power_changed(links));
}

static bool back_pressed(const CoverFillerLinks *links, bool slow_checks)
{
    HalKeyTransition transition;
    bool back = false;
    (void)links;
    (void)slow_checks;
    while (hal_input_next_transition(&transition)) {
        back |= transition.pressed && transition.key == KEY_BACK;
    }
    return back;
}

static bool session_stopped(void *context)
{
    FillSession *session = context;
    CoverFiller *filler = session->filler;
    if (filler->stop) {
        return true;
    }
    uint32_t now_us = hal_clock_microseconds();
    bool slow_checks = now_us - filler->polled_us >= COVER_FILLER_POLL_US;
    filler->polled_us = slow_checks ? now_us : filler->polled_us;
    filler->stop = session->check(session->links, slow_checks);
    return filler->stop;
}

static void find_queued(const CoverFillerLinks *links, const uint32_t *keys, uint32_t count,
                        uint16_t *found)
{
    uint32_t albums = library_count(links->library, LIBRARY_ALBUMS);
    for (uint32_t index = 0; index < count; index++) {
        found[index] = NO_ALBUM;
    }
    for (uint32_t album = 0; album < albums; album++) {
        uint32_t key = library_album_key(links->library, album);
        for (uint32_t index = 0; index < count; index++) {
            found[index] = keys[index] == key ? (uint16_t)album : found[index];
        }
    }
}

static void restore_queue(CoverFiller *filler, const CoverFillerLinks *links, const uint32_t *keys,
                          uint32_t count)
{
    uint16_t found[COVER_STATE_QUEUE];
    if (filler->queue_restored) {
        return;
    }
    filler->queue_restored = true;
    find_queued(links, keys, count, found);
    for (uint32_t index = count; index > 0; index--) {
        if (found[index - 1] != NO_ALBUM) {
            cover_queue_push(&filler->queue, found[index - 1], keys[index - 1]);
            filler->complete = false;
        }
    }
}

static void prune_queue(CoverFiller *filler, const CoverFillerLinks *links)
{
    for (uint32_t index = filler->queue.count; index > 0; index--) {
        uint16_t album = filler->queue.albums[index - 1];
        if (cover_cache_has(links->covers, album)) {
            cover_queue_remove(&filler->queue, album);
        }
    }
}

static void settle(CoverFiller *filler, const CoverFillerLinks *links, CoverFillOutcome outcome)
{
    if (outcome.stats.decoded > 0) {
        cover_cache_refresh(links->covers);
    }
    prune_queue(filler, links);
    if (outcome.result == SCAN_CANCELLED) {
        filler->quiet_since_ms = hal_clock_milliseconds();
        return;
    }
    filler->complete = outcome.result != SCAN_OK || outcome.stats.pending == 0;
}

static CoverFillRequest request_for(CoverFiller *filler, FillMode mode, FillSession *session,
                                    uint16_t *order)
{
    const uint16_t *current = filler->has_playing_album ? &filler->playing_album : NULL;
    return (CoverFillRequest){
        .priority = order,
        .priority_count = cover_queue_order(&filler->queue, current, order, COVER_QUEUE_CAPACITY),
        .budget = mode.budget,
        .poll = {session_stopped, session},
        .queue = {filler->queue_restored ? filler->queue.keys : NULL, filler->queue.count},
        .restart_when_done = mode.manual,
        .capacity = library_rescan_covers_capacity(),
        .progress = mode.progress,
    };
}

static CoverFillOutcome fill(CoverFiller *filler, const CoverFillerLinks *links, FillMode mode)
{
    uint16_t order[COVER_QUEUE_CAPACITY];
    uint32_t restored[COVER_STATE_QUEUE];
    uint32_t restored_count = 0;
    FillSession session = {.filler = filler, .links = links, .check = mode.check};
    CoverFillRequest request = request_for(filler, mode, &session, order);
    request.restored = (CoverRestoredKeys){restored, &restored_count};
    filler->stop = false;
    filler->polled_us = hal_clock_microseconds() - COVER_FILLER_POLL_US;
    filler->sessions++;
    CoverFillOutcome outcome = library_rescan_fill(&request);
    restore_queue(filler, links, restored, restored_count);
    settle(filler, links, outcome);
    return outcome;
}

CoverFillOutcome cover_filler_run_idle(CoverFiller *filler, const CoverFillerLinks *links)
{
    FillMode mode = {.budget = {COVER_FILL_SESSION_MS, COVER_FILL_SESSION_ALBUMS},
                     .check = idle_interrupted,
                     .manual = false,
                     .progress = NULL};
    return fill(filler, links, mode);
}

CoverFillOutcome cover_filler_run_all(CoverFiller *filler, const CoverFillerLinks *links,
                                      void (*progress)(uint32_t done, uint32_t total))
{
    FillMode mode = {.budget = {0, 0}, .check = back_pressed, .manual = true, .progress = progress};
    return fill(filler, links, mode);
}
