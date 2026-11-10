#include "cover_index.h"

#include <stddef.h>

#include "library/cover_build.h"
#include "library/covers.h"

enum {
    ALIGNMENT = 8,
    INDEX_CHUNK = 64,
    SECTOR_BYTES = 512,
    STAMP_FIELD = 12,
    STAMP_BYTES = 4,
    SLOT_BYTES = COVERS_ENTRY_SIZE + STAMP_BYTES + COVER_BUILD_RECORD_BYTES,
    BITS_PER_BYTE = 8
};

typedef struct RowRange {
    bool stamps;
    uint32_t first;
    uint32_t end;
} RowRange;

void *cover_arena_take(CoverArena *arena, uint32_t bytes)
{
    uintptr_t address = (uintptr_t)arena->cursor;
    uint8_t *start = arena->cursor + (ALIGNMENT - address % ALIGNMENT) % ALIGNMENT;
    if (start > arena->end || (uint32_t)(arena->end - start) < bytes) {
        return NULL;
    }
    arena->cursor = start + bytes;
    return start;
}

static void clear(uint8_t *bytes, uint32_t length)
{
    for (uint32_t index = 0; index < length; index++) {
        bytes[index] = 0;
    }
}

static uint32_t bit_bytes(uint32_t bits)
{
    return (bits + BITS_PER_BYTE - 1) / BITS_PER_BYTE;
}

static bool bit_set(const uint8_t *bits, uint32_t index)
{
    return (bits[index / BITS_PER_BYTE] >> (index % BITS_PER_BYTE) & 1u) != 0;
}

static void set_bit(uint8_t *bits, uint32_t index, bool value)
{
    uint8_t mask = (uint8_t)(1u << (index % BITS_PER_BYTE));
    bits[index / BITS_PER_BYTE] =
        (uint8_t)(value ? bits[index / BITS_PER_BYTE] | mask : bits[index / BITS_PER_BYTE] & ~mask);
}

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

static uint32_t get32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

CoverLayout cover_layout_for(uint32_t capacity)
{
    uint32_t room = capacity > COVERS_HEADER_SIZE ? capacity - COVERS_HEADER_SIZE : 0;
    uint32_t slots = room / SLOT_BYTES;
    slots = slots < COVER_BUILD_MAX_COVERS ? slots : COVER_BUILD_MAX_COVERS;
    uint32_t stamps_start = COVERS_HEADER_SIZE + slots * COVERS_ENTRY_SIZE;
    return (CoverLayout){slots, stamps_start, stamps_start + slots * STAMP_BYTES};
}

uint32_t cover_index_previous_count(ByteSource previous, uint32_t capacity, CoverLayout layout)
{
    uint8_t header[COVERS_HEADER_SIZE];
    Covers covers;
    if (previous.read == NULL || !covers_open(&covers, previous, capacity) ||
        !previous.read(previous.context, 0, header, sizeof header)) {
        return 0;
    }
    bool stamped = get32(header + STAMP_FIELD) == layout.stamps_start;
    return stamped && covers.count <= layout.slots ? covers.count : 0;
}

bool cover_index_carve(CoverIndex *index, CoverArena *arena, CoverLayout layout, uint32_t capacity)
{
    index->entries = cover_arena_take(arena, capacity * sizeof(CoverEntry) + 1);
    index->slots = cover_arena_take(arena, capacity * sizeof(uint16_t) + 1);
    index->claimed = cover_arena_take(arena, capacity + 1);
    index->used_slots = cover_arena_take(arena, bit_bytes(layout.slots) + 1);
    if (index->entries == NULL || index->slots == NULL || index->claimed == NULL ||
        index->used_slots == NULL) {
        return false;
    }
    clear(index->used_slots, bit_bytes(layout.slots));
    index->layout = layout;
    index->capacity = capacity;
    index->count = 0;
    index->next_slot = 0;
    return true;
}

static bool slot_of(const CoverIndex *index, uint32_t offset, uint32_t *slot)
{
    const CoverLayout *layout = &index->layout;
    if (offset < layout->records_start) {
        return false;
    }
    uint32_t delta = offset - layout->records_start;
    *slot = delta / COVER_BUILD_RECORD_BYTES;
    return delta % COVER_BUILD_RECORD_BYTES == 0 && *slot < layout->slots;
}

static void set_row(CoverIndex *index, uint32_t position, CoverEntry entry, uint32_t slot)
{
    index->entries[position] = entry;
    index->slots[position] = (uint16_t)slot;
    index->claimed[position] = 1;
    set_bit(index->used_slots, slot, true);
}

static bool accept_entry(CoverIndex *index, CoverEntry entry, uint32_t offset)
{
    uint32_t slot = 0;
    bool ordered = index->count == 0 || index->entries[index->count - 1].key < entry.key;
    if (index->count == index->capacity || !ordered || !slot_of(index, offset, &slot) ||
        bit_set(index->used_slots, slot)) {
        return false;
    }
    set_row(index, index->count++, entry, slot);
    return true;
}

static bool load_chunk(CoverIndex *index, ByteSource source, uint32_t first, uint32_t count)
{
    uint8_t entries[INDEX_CHUNK * COVERS_ENTRY_SIZE];
    uint8_t stamps[INDEX_CHUNK * STAMP_BYTES];
    uint32_t entries_at = COVERS_HEADER_SIZE + first * COVERS_ENTRY_SIZE;
    uint32_t stamps_at = index->layout.stamps_start + first * STAMP_BYTES;
    if (!source.read(source.context, entries_at, entries, count * COVERS_ENTRY_SIZE) ||
        !source.read(source.context, stamps_at, stamps, count * STAMP_BYTES)) {
        return false;
    }
    for (uint32_t row = 0; row < count; row++) {
        const uint8_t *entry = entries + row * COVERS_ENTRY_SIZE;
        CoverEntry loaded = {get32(entry), get32(stamps + row * STAMP_BYTES)};
        if (!accept_entry(index, loaded, get32(entry + 4))) {
            return false;
        }
    }
    return true;
}

void cover_index_load(CoverIndex *index, ByteSource previous, uint32_t count)
{
    bool loaded = true;
    for (uint32_t first = 0; loaded && first < count; first += INDEX_CHUNK) {
        uint32_t rows = count - first < INDEX_CHUNK ? count - first : INDEX_CHUNK;
        loaded = load_chunk(index, previous, first, rows);
    }
    if (!loaded) {
        index->count = 0;
        clear(index->used_slots, bit_bytes(index->layout.slots));
    }
    clear(index->claimed, index->count);
}

static bool write_header(const CoverIndex *index)
{
    uint8_t header[COVERS_HEADER_SIZE] = {'E', 'M', 'C', 'V'};
    put16(header + 4, COVERS_VERSION);
    put16(header + 6, index->count);
    put16(header + 8, COVER_FULL_SIZE);
    put16(header + 10, COVER_HALF_SIZE);
    put32(header + STAMP_FIELD, index->layout.stamps_start);
    return index->sink.write(index->sink.context, 0, header, sizeof header);
}

bool cover_index_clear(const CoverIndex *index)
{
    uint8_t header[COVERS_HEADER_SIZE] = {0};
    return index->sink.write(index->sink.context, 0, header, sizeof header);
}

bool cover_index_find(const CoverIndex *index, uint32_t key, uint32_t *position)
{
    uint32_t low = 0;
    uint32_t high = index->count;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2;
        low = index->entries[middle].key < key ? middle + 1 : low;
        high = index->entries[middle].key < key ? high : middle;
    }
    *position = low;
    return low < index->count && index->entries[low].key == key;
}

bool cover_index_reusable(const CoverIndex *index, uint32_t position, uint32_t stamp)
{
    uint32_t stored = index->entries[position].stamp;
    return stored != 0 && stored == stamp;
}

void cover_index_claim(CoverIndex *index, uint32_t position)
{
    index->claimed[position] = 1;
}

bool cover_index_claimed(const CoverIndex *index, uint32_t position)
{
    return index->claimed[position] != 0;
}

bool cover_index_free_slot(CoverIndex *index, uint32_t *slot)
{
    while (index->next_slot < index->layout.slots && bit_set(index->used_slots, index->next_slot)) {
        index->next_slot++;
    }
    *slot = index->next_slot;
    return index->next_slot < index->layout.slots;
}

bool cover_index_has_room(CoverIndex *index)
{
    uint32_t slot = 0;
    return index->count < index->capacity && cover_index_free_slot(index, &slot);
}

uint32_t cover_index_record_offset(const CoverIndex *index, uint32_t slot)
{
    return index->layout.records_start + slot * COVER_BUILD_RECORD_BYTES;
}

static void release_slot(CoverIndex *index, uint32_t slot)
{
    set_bit(index->used_slots, slot, false);
    index->next_slot = slot < index->next_slot ? slot : index->next_slot;
}

static void move_row(CoverIndex *index, uint32_t from, uint32_t to)
{
    index->entries[to] = index->entries[from];
    index->slots[to] = index->slots[from];
    index->claimed[to] = index->claimed[from];
}

static uint32_t row_width(bool stamps)
{
    return stamps ? STAMP_BYTES : COVERS_ENTRY_SIZE;
}

static uint32_t row_offset(const CoverIndex *index, bool stamps, uint32_t row)
{
    uint32_t start = stamps ? index->layout.stamps_start : COVERS_HEADER_SIZE;
    return start + row * row_width(stamps);
}

static uint32_t sector_start_row(uint32_t row)
{
    uint32_t sector = (COVERS_HEADER_SIZE + row * COVERS_ENTRY_SIZE) / SECTOR_BYTES * SECTOR_BYTES;
    return sector > COVERS_HEADER_SIZE ? (sector - COVERS_HEADER_SIZE) / COVERS_ENTRY_SIZE : 0;
}

static uint32_t sector_end_row(uint32_t row)
{
    uint32_t sector = (COVERS_HEADER_SIZE + row * COVERS_ENTRY_SIZE) / SECTOR_BYTES + 1;
    return (sector * SECTOR_BYTES - COVERS_HEADER_SIZE) / COVERS_ENTRY_SIZE;
}

static void pack_row(const CoverIndex *index, bool stamps, uint32_t row, uint8_t *bytes)
{
    if (stamps) {
        put32(bytes, index->entries[row].stamp);
        return;
    }
    put32(bytes, index->entries[row].key);
    put32(bytes + 4, cover_index_record_offset(index, index->slots[row]));
}

static bool write_rows(const CoverIndex *index, RowRange range)
{
    uint8_t bytes[SECTOR_BYTES];
    uint32_t width = row_width(range.stamps);
    for (uint32_t row = range.first; row < range.end; row++) {
        pack_row(index, range.stamps, row, bytes + (row - range.first) * width);
    }
    return index->sink.write(index->sink.context, row_offset(index, range.stamps, range.first),
                             bytes, (range.end - range.first) * width);
}

static bool write_pair(const CoverIndex *index, uint32_t first, uint32_t end)
{
    return write_rows(index, (RowRange){false, first, end}) &&
           write_rows(index, (RowRange){true, first, end});
}

static bool write_ascending(const CoverIndex *index, uint32_t first)
{
    for (uint32_t row = first; row < index->count;) {
        uint32_t end = sector_end_row(row);
        end = end < index->count ? end : index->count;
        if (!write_pair(index, row, end)) {
            return false;
        }
        row = end;
    }
    return true;
}

static bool write_descending(const CoverIndex *index, uint32_t first)
{
    for (uint32_t end = index->count; end > first;) {
        uint32_t row = sector_start_row(end - 1);
        row = row > first ? row : first;
        if (!write_pair(index, row, end)) {
            return false;
        }
        end = row;
    }
    return true;
}

static bool commit_grown(const CoverIndex *index, uint32_t first)
{
    return write_ascending(index, first) && write_header(index);
}

static bool commit_shrunk(const CoverIndex *index, uint32_t first)
{
    return write_header(index) && write_descending(index, first);
}

bool cover_index_add(CoverIndex *index, uint32_t position, CoverEntry entry, uint32_t slot)
{
    for (uint32_t row = index->count; row > position; row--) {
        move_row(index, row - 1, row);
    }
    index->count++;
    set_row(index, position, entry, slot);
    return commit_grown(index, position);
}

bool cover_index_replace(CoverIndex *index, uint32_t position, CoverEntry entry, uint32_t slot)
{
    uint32_t old = index->slots[position];
    set_row(index, position, entry, slot);
    if (!write_pair(index, position, position + 1)) {
        return false;
    }
    release_slot(index, old);
    return true;
}

bool cover_index_retire(CoverIndex *index, uint32_t position, uint32_t *slot)
{
    *slot = index->slots[position];
    for (uint32_t row = position; row + 1 < index->count; row++) {
        move_row(index, row + 1, row);
    }
    index->count--;
    return commit_shrunk(index, position);
}

bool cover_index_drop_unclaimed(CoverIndex *index)
{
    uint32_t kept = 0;
    uint32_t first = index->count;
    for (uint32_t row = 0; row < index->count; row++) {
        if (index->claimed[row] != 0) {
            move_row(index, row, kept++);
            continue;
        }
        release_slot(index, index->slots[row]);
        first = first < row ? first : row;
    }
    bool dropped = kept < index->count;
    index->count = kept;
    clear(index->claimed, kept);
    return !dropped || commit_shrunk(index, first);
}
