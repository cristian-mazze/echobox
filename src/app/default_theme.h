#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ui/theme.h"

extern const Theme DEFAULT_THEME;

Theme default_theme_for(bool portrait);
void default_theme_set_accent(Theme *theme, uint8_t accent);
