#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    COVER_STATE_QUEUE = 64,
    COVER_STATE_FAILURES = 2673,
    COVER_STATE_HEADER_BYTES = 24,
    COVER_STATE_BYTES = COVER_STATE_HEADER_BYTES + COVER_STATE_QUEUE * 4 + COVER_STATE_FAILURES * 8,
    COVER_STATE_FILE_BYTES = 24 * 1024
};

void cover_state_reset(uint8_t *bytes);
bool cover_state_valid(const uint8_t *bytes);
void cover_state_seal(uint8_t *bytes);
void cover_state_open(uint8_t *bytes, uint32_t fingerprint);
uint32_t cover_state_cursor(const uint8_t *bytes);
void cover_state_set_cursor(uint8_t *bytes, uint32_t cursor);
uint32_t cover_state_queue_count(const uint8_t *bytes);
uint32_t cover_state_queue_key(const uint8_t *bytes, uint32_t index);
void cover_state_set_queue(uint8_t *bytes, const uint32_t *keys, uint32_t count);
uint32_t cover_state_failure_count(const uint8_t *bytes);
bool cover_state_failed(const uint8_t *bytes, uint32_t key, uint32_t stamp);
void cover_state_note_failure(uint8_t *bytes, uint32_t key, uint32_t stamp);
void cover_state_forget_failure(uint8_t *bytes, uint32_t key);
