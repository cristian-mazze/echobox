#include "cover_state_store.h"

#include "hal/storage.h"
#include "library/cover_state.h"

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

bool cover_state_store_save(uint8_t *bytes)
{
    cover_state_seal(bytes);
    return hal_file_write_at(COVER_STATE_PATH, 0, bytes, COVER_STATE_BYTES);
}
