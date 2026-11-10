#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cover_art_rules.h"
#include "library/byte_source.h"
#include "library/library_builder.h"

enum {
    ART_FACTS_BLOCK = 512,
    ART_FACTS_MAX_ROWS = 0xFFFF,
    ART_FACTS_HEADER_BYTES = ART_FACTS_BLOCK,
    ART_FACT_TRACK_BYTES = 16,
    ART_FACT_FOLDER_BYTES = 32,
    ART_FACTS_TRACKS_BYTES = ART_FACTS_MAX_ROWS * ART_FACT_TRACK_BYTES + ART_FACTS_BLOCK,
    ART_FACTS_FOLDERS_BYTES = ART_FACTS_MAX_ROWS * ART_FACT_FOLDER_BYTES + ART_FACTS_BLOCK,
    ART_FACTS_BYTES = ART_FACTS_HEADER_BYTES + ART_FACTS_TRACKS_BYTES + ART_FACTS_FOLDERS_BYTES,
    ART_FACTS_MIN_SCRATCH = ART_FACTS_BYTES * 4
};

typedef struct ArtTrackFact {
    uint32_t size;
    uint32_t picture_offset;
    uint32_t picture_length;
    uint16_t folder;
    uint8_t picture_kind;
} ArtTrackFact;

typedef struct ArtLog {
    uint8_t block[ART_FACTS_BLOCK];
    uint32_t start;
    uint32_t first_row;
    uint32_t rows;
    uint32_t width;
} ArtLog;

typedef struct ArtFactsWriter {
    BuilderScratch scratch;
    uint32_t region;
    bool enabled;
    ArtLog tracks;
    ArtLog folders;
} ArtFactsWriter;

typedef struct ArtFacts {
    ByteSource source;
    uint32_t region;
    uint32_t tracks;
    uint32_t folders;
    uint32_t track_order;
    bool valid;
} ArtFacts;

BuilderScratch art_facts_open(ArtFactsWriter *writer, BuilderScratch scratch);
uint16_t art_facts_add_folder(ArtFactsWriter *writer, const CoverArtPick *pick);
void art_facts_add_track(ArtFactsWriter *writer, const ArtTrackFact *fact);
void art_facts_close(ArtFactsWriter *writer, uint32_t track_order);
bool art_facts_load(ArtFacts *facts, ByteSource scratch, uint32_t scratch_size, uint32_t tracks);
bool art_facts_track(const ArtFacts *facts, uint32_t track, ArtTrackFact *fact);
bool art_facts_folder(const ArtFacts *facts, uint32_t folder, CoverArtPick *pick);
