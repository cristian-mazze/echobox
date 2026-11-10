#pragma once

#include "player.h"

PlayerStatus app_player_status(void);
const char *app_toast_text(void);
bool app_covers_syncing(void);
const char *app_diagnostics_value(int index);
