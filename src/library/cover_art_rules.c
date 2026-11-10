#include "cover_art_rules.h"

enum {
    SUFFIX_COUNT = 3
};

static const char *const STEMS[COVER_ART_STEMS] = {"cover", "folder", "front", "albumart"};
static const char *const SUFFIXES[SUFFIX_COUNT] = {".jpg", ".jpeg", ".png"};

static char lower(char character)
{
    return character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a') : character;
}

static bool is_name(const char *name, const char *stem, const char *suffix)
{
    while (*stem != '\0') {
        if (lower(*name++) != *stem++) {
            return false;
        }
    }
    while (*suffix != '\0') {
        if (lower(*name++) != *suffix++) {
            return false;
        }
    }
    return *name == '\0';
}

static bool before(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) {
        left++;
        right++;
    }
    return (uint8_t)*left < (uint8_t)*right;
}

static uint32_t stem_of(const char *name)
{
    for (uint32_t stem = 0; stem < COVER_ART_STEMS; stem++) {
        for (uint32_t suffix = 0; suffix < SUFFIX_COUNT; suffix++) {
            if (is_name(name, STEMS[stem], SUFFIXES[suffix])) {
                return stem;
            }
        }
    }
    return COVER_ART_STEMS;
}

static void copy_name(char *target, const char *name)
{
    uint32_t index = 0;
    while (name[index] != '\0' && index + 1 < COVER_ART_NAME_CAPACITY) {
        target[index] = name[index];
        index++;
    }
    target[index] = '\0';
}

void cover_art_pick_reset(CoverArtPick *pick)
{
    pick->name[0] = '\0';
    pick->modified = 0;
    pick->size = 0;
    pick->stem = COVER_ART_STEMS;
}

void cover_art_pick_offer(CoverArtPick *pick, const ScanEntry *entry)
{
    uint32_t stem = entry->directory ? COVER_ART_STEMS : stem_of(entry->name);
    bool better = stem < pick->stem ||
                  (stem == pick->stem && stem < COVER_ART_STEMS && before(entry->name, pick->name));
    if (!better) {
        return;
    }
    copy_name(pick->name, entry->name);
    pick->modified = entry->modified;
    pick->size = entry->size;
    pick->stem = stem;
}

bool cover_art_pick_found(const CoverArtPick *pick)
{
    return pick->stem < COVER_ART_STEMS;
}
