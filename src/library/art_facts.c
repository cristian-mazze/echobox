#include "art_facts.h"

#include <stddef.h>

enum {
    MAGIC = 0x54464145u,
    HEADER_FIELDS = 4,
    TRACKS_FIELD = 4,
    FOLDERS_FIELD = 8,
    ORDER_FIELD = 12,
    NAME_FIELD = 0,
    MODIFIED_FIELD = 16,
    SIZE_FIELD = 20,
    STEM_FIELD = 24,
    PICTURE_OFFSET_FIELD = 4,
    PICTURE_LENGTH_FIELD = 8,
    FOLDER_FIELD = 12,
    KIND_FIELD = 14,
    NO_FOLDER = 0xFFFF
};

static void put16(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *bytes, uint32_t value)
{
    put16(bytes, value);
    put16(bytes + 2, value >> 16);
}

static uint16_t get16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | bytes[1] << 8);
}

static uint32_t get32(const uint8_t *bytes)
{
    return (uint32_t)get16(bytes) | (uint32_t)get16(bytes + 2) << 16;
}

static void clear(uint8_t *bytes, uint32_t length)
{
    for (uint32_t index = 0; index < length; index++) {
        bytes[index] = 0;
    }
}

static uint32_t region_of(uint32_t scratch_size)
{
    return (scratch_size - ART_FACTS_BYTES) / ART_FACTS_BLOCK * ART_FACTS_BLOCK;
}

static uint32_t tracks_start(uint32_t region)
{
    return region + ART_FACTS_HEADER_BYTES;
}

static uint32_t folders_start(uint32_t region)
{
    return tracks_start(region) + ART_FACTS_TRACKS_BYTES;
}

static void log_open(ArtLog *log, uint32_t start, uint32_t width)
{
    log->start = start;
    log->first_row = 0;
    log->rows = 0;
    log->width = width;
}

static uint32_t log_count(const ArtLog *log)
{
    return log->first_row + log->rows;
}

static bool log_flush(ArtFactsWriter *writer, ArtLog *log)
{
    uint32_t offset = log->start + log->first_row * log->width;
    bool written = log->rows == 0 || writer->scratch.write(writer->scratch.context, offset,
                                                           log->block, log->rows * log->width);
    log->first_row += log->rows;
    log->rows = 0;
    return written;
}

static uint8_t *log_next(ArtFactsWriter *writer, ArtLog *log)
{
    if (log_count(log) >= ART_FACTS_MAX_ROWS) {
        writer->enabled = false;
        return NULL;
    }
    if ((log->rows + 1) * log->width > ART_FACTS_BLOCK && !log_flush(writer, log)) {
        writer->enabled = false;
        return NULL;
    }
    uint8_t *row = log->block + log->rows * log->width;
    clear(row, log->width);
    log->rows++;
    return row;
}

static bool write_header(const ArtFactsWriter *writer, const uint32_t *fields)
{
    uint8_t header[HEADER_FIELDS * 4];
    for (uint32_t field = 0; field < HEADER_FIELDS; field++) {
        put32(header + field * 4, fields[field]);
    }
    return writer->scratch.write(writer->scratch.context, writer->region, header, sizeof header);
}

BuilderScratch art_facts_open(ArtFactsWriter *writer, BuilderScratch scratch)
{
    static const uint32_t EMPTY[HEADER_FIELDS] = {0, 0, 0, 0};
    writer->scratch = scratch;
    writer->enabled = scratch.write != NULL && scratch.size >= ART_FACTS_MIN_SCRATCH;
    if (!writer->enabled) {
        return scratch;
    }
    writer->region = region_of(scratch.size);
    log_open(&writer->tracks, tracks_start(writer->region), ART_FACT_TRACK_BYTES);
    log_open(&writer->folders, folders_start(writer->region), ART_FACT_FOLDER_BYTES);
    writer->enabled = write_header(writer, EMPTY);
    scratch.size = writer->region;
    return scratch;
}

uint16_t art_facts_add_folder(ArtFactsWriter *writer, const CoverArtPick *pick)
{
    uint32_t folder = log_count(&writer->folders);
    uint8_t *row = writer->enabled ? log_next(writer, &writer->folders) : NULL;
    if (row == NULL) {
        return NO_FOLDER;
    }
    for (uint32_t index = 0; index + 1 < COVER_ART_NAME_CAPACITY && pick->name[index] != '\0';
         index++) {
        row[NAME_FIELD + index] = (uint8_t)pick->name[index];
    }
    put32(row + MODIFIED_FIELD, pick->modified);
    put32(row + SIZE_FIELD, pick->size);
    row[STEM_FIELD] = (uint8_t)pick->stem;
    return (uint16_t)folder;
}

void art_facts_add_track(ArtFactsWriter *writer, const ArtTrackFact *fact)
{
    uint8_t *row = writer->enabled ? log_next(writer, &writer->tracks) : NULL;
    if (row == NULL) {
        return;
    }
    put32(row, fact->size);
    put32(row + PICTURE_OFFSET_FIELD, fact->picture_offset);
    put32(row + PICTURE_LENGTH_FIELD, fact->picture_length);
    put16(row + FOLDER_FIELD, fact->folder);
    row[KIND_FIELD] = fact->picture_kind;
}

void art_facts_close(ArtFactsWriter *writer, uint32_t track_order)
{
    if (!writer->enabled || track_order == LIBRARY_BUILDER_NO_ORDER ||
        !log_flush(writer, &writer->tracks) || !log_flush(writer, &writer->folders)) {
        return;
    }
    const uint32_t fields[HEADER_FIELDS] = {MAGIC, log_count(&writer->tracks),
                                            log_count(&writer->folders), track_order};
    writer->enabled = write_header(writer, fields);
}

static bool read_bytes(const ArtFacts *facts, uint32_t offset, void *bytes, uint32_t length)
{
    return facts->source.read(facts->source.context, offset, bytes, length);
}

bool art_facts_load(ArtFacts *facts, ByteSource scratch, uint32_t scratch_size, uint32_t tracks)
{
    uint8_t header[HEADER_FIELDS * 4];
    facts->valid = false;
    if (scratch.read == NULL || scratch_size < ART_FACTS_MIN_SCRATCH) {
        return false;
    }
    facts->source = scratch;
    facts->region = region_of(scratch_size);
    if (!read_bytes(facts, facts->region, header, sizeof header) || get32(header) != MAGIC) {
        return false;
    }
    facts->tracks = get32(header + TRACKS_FIELD);
    facts->folders = get32(header + FOLDERS_FIELD);
    facts->track_order = get32(header + ORDER_FIELD);
    facts->valid = facts->tracks == tracks && facts->folders <= ART_FACTS_MAX_ROWS &&
                   facts->track_order <= facts->region &&
                   facts->region - facts->track_order >= tracks * 2;
    return facts->valid;
}

bool art_facts_track(const ArtFacts *facts, uint32_t track, ArtTrackFact *fact)
{
    uint8_t order[2];
    uint8_t row[ART_FACT_TRACK_BYTES];
    if (!facts->valid || track >= facts->tracks ||
        !read_bytes(facts, facts->track_order + track * 2, order, sizeof order) ||
        get16(order) >= facts->tracks ||
        !read_bytes(facts, tracks_start(facts->region) + get16(order) * ART_FACT_TRACK_BYTES, row,
                    sizeof row)) {
        return false;
    }
    fact->size = get32(row);
    fact->picture_offset = get32(row + PICTURE_OFFSET_FIELD);
    fact->picture_length = get32(row + PICTURE_LENGTH_FIELD);
    fact->folder = get16(row + FOLDER_FIELD);
    fact->picture_kind = row[KIND_FIELD];
    return true;
}

bool art_facts_folder(const ArtFacts *facts, uint32_t folder, CoverArtPick *pick)
{
    uint8_t row[ART_FACT_FOLDER_BYTES];
    uint32_t offset = folders_start(facts->region) + folder * ART_FACT_FOLDER_BYTES;
    if (!facts->valid || folder >= facts->folders || !read_bytes(facts, offset, row, sizeof row) ||
        row[NAME_FIELD + COVER_ART_NAME_CAPACITY - 1] != 0) {
        return false;
    }
    for (uint32_t index = 0; index < COVER_ART_NAME_CAPACITY; index++) {
        pick->name[index] = (char)row[NAME_FIELD + index];
    }
    pick->modified = get32(row + MODIFIED_FIELD);
    pick->size = get32(row + SIZE_FIELD);
    pick->stem = row[STEM_FIELD];
    return true;
}
