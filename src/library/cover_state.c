#include "library/cover_state.h"

#include "core/hash.h"

enum {
    VERSION = 2,
    FIRST_VERSION = 1,
    VERSION_FIELD = 4,
    QUEUE_COUNT_FIELD = 6,
    FAILURE_COUNT_FIELD = 8,
    PENDING_FIELD = 10,
    CURSOR_FIELD = 12,
    FINGERPRINT_FIELD = 16,
    CHECKSUM_FIELD = 20,
    QUEUE_FIELD = COVER_STATE_HEADER_BYTES,
    FAILURES_FIELD = QUEUE_FIELD + COVER_STATE_QUEUE * 4,
    FAILURE_BYTES = 8,
    MAGIC_SIZE = 4
};

static const uint8_t MAGIC[MAGIC_SIZE] = {'E', 'M', 'C', 'S'};

static void put16(uint8_t *bytes, uint32_t offset, uint32_t value)
{
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *bytes, uint32_t offset, uint32_t value)
{
    put16(bytes, offset, value & 0xFFFFu);
    put16(bytes, offset + 2, value >> 16);
}

static uint32_t get16(const uint8_t *bytes, uint32_t offset)
{
    return (uint32_t)bytes[offset] | (uint32_t)bytes[offset + 1] << 8;
}

static uint32_t get32(const uint8_t *bytes, uint32_t offset)
{
    return get16(bytes, offset) | get16(bytes, offset + 2) << 16;
}

uint32_t cover_state_check_start(const uint8_t *header)
{
    return hash_update(HASH_START, header, CHECKSUM_FIELD);
}

static uint32_t checksum_of(const uint8_t *bytes)
{
    uint32_t hash = cover_state_check_start(bytes);
    return hash_update(hash, bytes + QUEUE_FIELD, COVER_STATE_BYTES - QUEUE_FIELD);
}

void cover_state_reset(uint8_t *bytes)
{
    for (uint32_t index = 0; index < COVER_STATE_BYTES; index++) {
        bytes[index] = 0;
    }
    for (uint32_t index = 0; index < MAGIC_SIZE; index++) {
        bytes[index] = MAGIC[index];
    }
    put16(bytes, VERSION_FIELD, VERSION);
    put16(bytes, PENDING_FIELD, COVER_STATE_PENDING_UNKNOWN);
}

static bool header_valid(const uint8_t *header)
{
    for (uint32_t index = 0; index < MAGIC_SIZE; index++) {
        if (header[index] != MAGIC[index]) {
            return false;
        }
    }
    uint32_t version = get16(header, VERSION_FIELD);
    return version >= FIRST_VERSION && version <= VERSION &&
           get16(header, QUEUE_COUNT_FIELD) <= COVER_STATE_QUEUE &&
           get16(header, FAILURE_COUNT_FIELD) <= COVER_STATE_FAILURES;
}

bool cover_state_check_finish(const uint8_t *header, uint32_t hash)
{
    return header_valid(header) && get32(header, CHECKSUM_FIELD) == hash;
}

bool cover_state_valid(const uint8_t *bytes)
{
    return cover_state_check_finish(bytes, checksum_of(bytes));
}

static void upgrade(uint8_t *bytes)
{
    if (get16(bytes, VERSION_FIELD) < VERSION) {
        put16(bytes, VERSION_FIELD, VERSION);
        put16(bytes, PENDING_FIELD, COVER_STATE_PENDING_UNKNOWN);
    }
}

void cover_state_seal(uint8_t *bytes)
{
    put32(bytes, CHECKSUM_FIELD, checksum_of(bytes));
}

void cover_state_open(uint8_t *bytes, uint32_t fingerprint)
{
    if (!cover_state_valid(bytes)) {
        cover_state_reset(bytes);
    }
    upgrade(bytes);
    if (get32(bytes, FINGERPRINT_FIELD) != fingerprint) {
        put32(bytes, FINGERPRINT_FIELD, fingerprint);
        put32(bytes, CURSOR_FIELD, 0);
        put16(bytes, PENDING_FIELD, COVER_STATE_PENDING_UNKNOWN);
    }
}

uint32_t cover_state_cursor(const uint8_t *bytes)
{
    return get32(bytes, CURSOR_FIELD);
}

void cover_state_set_cursor(uint8_t *bytes, uint32_t cursor)
{
    put32(bytes, CURSOR_FIELD, cursor);
}

uint32_t cover_state_pending(const uint8_t *bytes)
{
    return get16(bytes, VERSION_FIELD) < VERSION ? COVER_STATE_PENDING_UNKNOWN
                                                 : get16(bytes, PENDING_FIELD);
}

void cover_state_set_pending(uint8_t *bytes, uint32_t pending)
{
    bool unknown = pending == COVER_STATE_PENDING_UNKNOWN;
    uint32_t kept = pending > COVER_STATE_PENDING_MAX ? COVER_STATE_PENDING_MAX : pending;
    put16(bytes, PENDING_FIELD, unknown ? COVER_STATE_PENDING_UNKNOWN : kept);
}

uint32_t cover_state_queue_count(const uint8_t *bytes)
{
    return get16(bytes, QUEUE_COUNT_FIELD);
}

uint32_t cover_state_queue_key(const uint8_t *bytes, uint32_t index)
{
    return get32(bytes, QUEUE_FIELD + index * 4);
}

static bool queued(const uint8_t *bytes, uint32_t count, uint32_t key)
{
    for (uint32_t index = 0; index < count; index++) {
        if (cover_state_queue_key(bytes, index) == key) {
            return true;
        }
    }
    return false;
}

void cover_state_set_queue(uint8_t *bytes, const uint32_t *keys, uint32_t count)
{
    uint32_t kept = 0;
    for (uint32_t index = 0; index < count && kept < COVER_STATE_QUEUE; index++) {
        if (!queued(bytes, kept, keys[index])) {
            put32(bytes, QUEUE_FIELD + kept++ * 4, keys[index]);
        }
    }
    for (uint32_t index = kept; index < COVER_STATE_QUEUE; index++) {
        put32(bytes, QUEUE_FIELD + index * 4, 0);
    }
    put16(bytes, QUEUE_COUNT_FIELD, kept);
}

uint32_t cover_state_failure_count(const uint8_t *bytes)
{
    return get16(bytes, FAILURE_COUNT_FIELD);
}

static uint32_t failure_key(const uint8_t *bytes, uint32_t index)
{
    return get32(bytes, FAILURES_FIELD + index * FAILURE_BYTES);
}

static bool find_failure(const uint8_t *bytes, uint32_t key, uint32_t *position)
{
    uint32_t low = 0;
    uint32_t high = cover_state_failure_count(bytes);
    while (low < high) {
        uint32_t middle = low + (high - low) / 2;
        low = failure_key(bytes, middle) < key ? middle + 1 : low;
        high = failure_key(bytes, middle) < key ? high : middle;
    }
    *position = low;
    return low < cover_state_failure_count(bytes) && failure_key(bytes, low) == key;
}

bool cover_state_failed(const uint8_t *bytes, uint32_t key, uint32_t stamp)
{
    uint32_t position = 0;
    return find_failure(bytes, key, &position) &&
           get32(bytes, FAILURES_FIELD + position * FAILURE_BYTES + 4) == stamp;
}

static void move_failures(uint8_t *bytes, uint32_t from, uint32_t to, uint32_t count)
{
    uint8_t *base = bytes + FAILURES_FIELD;
    if (to > from) {
        for (uint32_t index = count * FAILURE_BYTES; index > 0; index--) {
            base[to * FAILURE_BYTES + index - 1] = base[from * FAILURE_BYTES + index - 1];
        }
        return;
    }
    for (uint32_t index = 0; index < count * FAILURE_BYTES; index++) {
        base[to * FAILURE_BYTES + index] = base[from * FAILURE_BYTES + index];
    }
}

void cover_state_note_failure(uint8_t *bytes, uint32_t key, uint32_t stamp)
{
    uint32_t position = 0;
    uint32_t count = cover_state_failure_count(bytes);
    if (!find_failure(bytes, key, &position)) {
        if (count == COVER_STATE_FAILURES) {
            return;
        }
        move_failures(bytes, position, position + 1, count - position);
        put16(bytes, FAILURE_COUNT_FIELD, count + 1);
    }
    put32(bytes, FAILURES_FIELD + position * FAILURE_BYTES, key);
    put32(bytes, FAILURES_FIELD + position * FAILURE_BYTES + 4, stamp);
}

void cover_state_forget_failure(uint8_t *bytes, uint32_t key)
{
    uint32_t position = 0;
    uint32_t count = cover_state_failure_count(bytes);
    if (!find_failure(bytes, key, &position)) {
        return;
    }
    move_failures(bytes, position + 1, position, count - position - 1);
    put32(bytes, FAILURES_FIELD + (count - 1) * FAILURE_BYTES, 0);
    put32(bytes, FAILURES_FIELD + (count - 1) * FAILURE_BYTES + 4, 0);
    put16(bytes, FAILURE_COUNT_FIELD, count - 1);
}
