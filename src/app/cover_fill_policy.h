#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    COVER_FILL_QUIET_MS = 20000,
    COVER_FILL_MIN_BATTERY = 20,
    COVER_FILL_SESSION_MS = 3000,
    COVER_FILL_SESSION_ALBUMS = 4
};

typedef struct CoverFillConditions {
    bool enabled;
    bool playing;
    uint32_t quiet_since_ms;
    bool display_on;
    bool usb_power;
    bool battery_known;
    uint8_t battery_percent;
    bool restart_pending;
    bool rescan_waiting;
    bool work_left;
} CoverFillConditions;

bool cover_fill_ready(const CoverFillConditions *conditions, uint32_t now_ms);
