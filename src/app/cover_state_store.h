#pragma once

#include <stdbool.h>
#include <stdint.h>

extern const char COVER_STATE_PATH[];

void cover_state_store_load(uint8_t *bytes, uint32_t fingerprint);
bool cover_state_store_save(uint8_t *bytes);
uint32_t cover_state_store_pending(void);
