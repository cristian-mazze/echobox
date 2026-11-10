#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/library_scan.h"

enum {
    COVER_ART_NAME_CAPACITY = 16,
    COVER_ART_STEMS = 4
};

typedef struct CoverArtPick {
    char name[COVER_ART_NAME_CAPACITY];
    uint32_t modified;
    uint32_t size;
    uint32_t stem;
} CoverArtPick;

void cover_art_pick_reset(CoverArtPick *pick);
void cover_art_pick_offer(CoverArtPick *pick, const ScanEntry *entry);
bool cover_art_pick_found(const CoverArtPick *pick);
