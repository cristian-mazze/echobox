#include <stddef.h>
#include <stdint.h>

#include "board.h"
#include "device/image_crc.h"
#include "hal/storage.h"
#include "hal/system.h"

#define PLAY_BASE 0x03092000u
#define PLAY_CAPACITY 0xD000u
#define PLAY_MAGIC 0x4C564F45u

enum {
    HEADER_BYTES = 16
};

static const char PLAY_OVERLAY_PATH[] = "/.echo/play_overlay.bin";

static uint32_t le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

static bool header_valid(const uint8_t *bytes, uint32_t file_size)
{
    uint32_t size = le32(bytes + 4);
    return le32(bytes) == PLAY_MAGIC && size >= HEADER_BYTES && size <= PLAY_CAPACITY &&
           size <= file_size;
}

static bool read_play(HalFile file)
{
    uint8_t *target = (uint8_t *)(uintptr_t)PLAY_BASE;
    if (!hal_file_read(file, 0, target, HEADER_BYTES) || !header_valid(target, file.size)) {
        return false;
    }
    uint32_t size = le32(target + 4);
    uint32_t crc = le32(target + 8);
    return hal_file_read(file, HEADER_BYTES, target + HEADER_BYTES, size - HEADER_BYTES) &&
           image_crc_update(0, target + HEADER_BYTES, size - HEADER_BYTES) == crc;
}

const void *hal_system_load_play_code(void)
{
    HalFile file;
    if (hal_system_restart_pending() || !hal_file_open(&file, PLAY_OVERLAY_PATH)) {
        return NULL;
    }
    bool loaded = read_play(file);
    hal_file_close(&file);
    if (!loaded) {
        board_glyphs_start();
        return NULL;
    }
    __asm__ volatile("dsb\n\tisb" ::: "memory");
    return (const uint8_t *)(uintptr_t)PLAY_BASE + HEADER_BYTES;
}
