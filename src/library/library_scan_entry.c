#include "library/cover_build.h"
#include "library/cover_play.h"
#include "library/library_scan.h"

const LibraryScanEntry LIBRARY_SCAN_ENTRY = {
    .magic = LIBRARY_SCAN_MAGIC,
    .scan = library_scan,
    .reconcile = cover_reconcile,
    .fill = cover_fill,
#ifdef ECHO_PLAY_FILL
    .play_place = cover_play_place,
#endif
};
