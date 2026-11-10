#include "cover_state_store.h"

#include "core/hash.h"
#include "hal/storage.h"
#include "library/cover_state.h"

enum {
    PEEK_CHUNK = 1024
};

const char COVER_STATE_PATH[] = "/.echo/covers_state.bin";

void cover_state_store_load(uint8_t *bytes, uint32_t fingerprint)
{
    HalFile file;
    bool read = hal_file_open(&file, COVER_STATE_PATH);
    if (read) {
        read = hal_file_read(file, 0, bytes, COVER_STATE_BYTES);
        hal_file_close(&file);
    }
    if (!read) {
        cover_state_reset(bytes);
    }
    cover_state_open(bytes, fingerprint);
}

static bool hash_rest(HalFile file, uint32_t *hash)
{
    uint8_t chunk[PEEK_CHUNK];
    for (uint32_t offset = COVER_STATE_HEADER_BYTES; offset < COVER_STATE_BYTES;
         offset += PEEK_CHUNK) {
        uint32_t length = COVER_STATE_BYTES - offset;
        length = length < PEEK_CHUNK ? length : PEEK_CHUNK;
        if (!hal_file_read(file, offset, chunk, length)) {
            return false;
        }
        *hash = hash_update(*hash, chunk, length);
    }
    return true;
}

static bool read_checked_header(HalFile file, uint8_t *header)
{
    if (!hal_file_read(file, 0, header, COVER_STATE_HEADER_BYTES)) {
        return false;
    }
    uint32_t hash = cover_state_check_start(header);
    return hash_rest(file, &hash) && cover_state_check_finish(header, hash);
}

uint32_t cover_state_store_pending(void)
{
    HalFile file;
    uint8_t header[COVER_STATE_HEADER_BYTES];
    if (!hal_file_open(&file, COVER_STATE_PATH)) {
        return COVER_STATE_PENDING_UNKNOWN;
    }
    bool valid = read_checked_header(file, header);
    hal_file_close(&file);
    return valid ? cover_state_pending(header) : COVER_STATE_PENDING_UNKNOWN;
}

bool cover_state_store_save(uint8_t *bytes)
{
    cover_state_seal(bytes);
    return hal_file_write_at(COVER_STATE_PATH, 0, bytes, COVER_STATE_BYTES);
}
