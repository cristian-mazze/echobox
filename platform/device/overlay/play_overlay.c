#include "library/cover_build.h"
#include "library/cover_play.h"
#include "library/library_scan.h"

const LibraryScanEntry PLAY_SCAN_ENTRY = {
    .magic = LIBRARY_SCAN_MAGIC,
    .fill = cover_fill,
    .play_place = cover_play_place,
};

const LibraryScanEntry *play_overlay_entry(void);

const LibraryScanEntry *play_overlay_entry(void)
{
    return &PLAY_SCAN_ENTRY;
}
