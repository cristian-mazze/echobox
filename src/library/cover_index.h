#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/byte_source.h"
#include "library/library_builder.h"

typedef struct CoverEntry {
    uint32_t key;
    uint32_t stamp;
} CoverEntry;

typedef struct CoverLayout {
    uint32_t slots;
    uint32_t stamps_start;
    uint32_t records_start;
} CoverLayout;

typedef struct CoverArena {
    uint8_t *cursor;
    uint8_t *end;
} CoverArena;

typedef struct CoverIndex {
    CoverLayout layout;
    ByteSink sink;
    CoverEntry *entries;
    uint16_t *slots;
    uint8_t *claimed;
    uint8_t *used_slots;
    uint32_t capacity;
    uint32_t count;
    uint32_t next_slot;
} CoverIndex;

void *cover_arena_take(CoverArena *arena, uint32_t bytes);
CoverLayout cover_layout_for(uint32_t capacity);
uint32_t cover_index_previous_count(ByteSource previous, uint32_t capacity, CoverLayout layout);
bool cover_index_carve(CoverIndex *index, CoverArena *arena, CoverLayout layout, uint32_t capacity);
void cover_index_load(CoverIndex *index, ByteSource previous, uint32_t count);
bool cover_index_clear(const CoverIndex *index);
bool cover_index_find(const CoverIndex *index, uint32_t key, uint32_t *position);
bool cover_index_reusable(const CoverIndex *index, uint32_t position, uint32_t stamp);
void cover_index_claim(CoverIndex *index, uint32_t position);
bool cover_index_claimed(const CoverIndex *index, uint32_t position);
bool cover_index_has_room(CoverIndex *index);
bool cover_index_free_slot(CoverIndex *index, uint32_t *slot);
uint32_t cover_index_record_offset(const CoverIndex *index, uint32_t slot);
bool cover_index_add(CoverIndex *index, uint32_t position, CoverEntry entry, uint32_t slot);
bool cover_index_replace(CoverIndex *index, uint32_t position, CoverEntry entry, uint32_t slot);
bool cover_index_retire(CoverIndex *index, uint32_t position, uint32_t *slot);
bool cover_index_drop_unclaimed(CoverIndex *index);
