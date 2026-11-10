#include "cover_fill_policy.h"

static bool quiet_long_enough(const CoverFillConditions *conditions, uint32_t now_ms)
{
    return !conditions->playing && now_ms - conditions->quiet_since_ms >= COVER_FILL_QUIET_MS;
}

static bool power_allows(const CoverFillConditions *conditions)
{
    return !conditions->usb_power && conditions->battery_known &&
           conditions->battery_percent >= COVER_FILL_MIN_BATTERY;
}

bool cover_fill_ready(const CoverFillConditions *conditions, uint32_t now_ms)
{
    return conditions->enabled && conditions->work_left && quiet_long_enough(conditions, now_ms) &&
           !conditions->display_on && power_allows(conditions) && !conditions->restart_pending &&
           !conditions->rescan_waiting;
}
