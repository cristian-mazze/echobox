#include "library/cover_play.h"

#include <stddef.h>

#include "cover_record_writer.h"
#include "cover_work.h"
#include "image/cover_image.h"

typedef struct PlayMemory {
    CoverRecordWriter *writer;
    CoverImageMemory decoder;
} PlayMemory;

static bool carve(const CoverState *state, PlayMemory *memory)
{
    ImageArena arena = image_arena_make(state->decode_memory, state->decode_size);
    memory->writer = image_arena_take(&arena, sizeof *memory->writer);
    memory->decoder = (CoverImageMemory){arena.base + arena.used, arena.size - arena.used};
    return memory->writer != NULL;
}

static CoverRowsRequest request_for(const CoverPlay *play, const ArtFile *art,
                                    const PlayMemory *memory)
{
    return (CoverRowsRequest){
        .source = {art->source.read, art->source.context, art->offset, art->length},
        .memory = memory->decoder,
        .sink = cover_record_writer_sink(memory->writer),
        .yield = play->yield,
        .jpeg_only = play->jpeg_only};
}

static Placement placement_of(CoverImageResult result)
{
    if (result == COVER_IMAGE_CANCELLED) {
        return PLACEMENT_CANCELLED;
    }
    if (result == COVER_IMAGE_NEEDS_IDLE || result == COVER_IMAGE_NO_MEMORY) {
        return PLACEMENT_DEFERRED;
    }
    return PLACEMENT_UNDECODABLE;
}

static bool index_cover(CoverState *state, uint32_t position, bool known, uint32_t slot,
                        uint32_t stamp)
{
    CoverEntry entry = {state->key, stamp};
    return known ? cover_index_replace(&state->index, position, entry, slot)
                 : cover_index_add(&state->index, position, entry, slot);
}

static Placement decode_into(CoverState *state, const CoverJob *job, const ArtFile *art,
                             uint32_t slot)
{
    PlayMemory memory;
    const CoverPlay *play = state->plan->play;
    if (!carve(state, &memory)) {
        return PLACEMENT_DEFERRED;
    }
    cover_record_writer_start(memory.writer, job->sink,
                              cover_index_record_offset(&state->index, slot));
    cover_record_writer_gate(memory.writer, play->may_write, play->context);
    CoverRowsRequest request = request_for(play, art, &memory);
    CoverImageResult result = cover_image_decode_rows(&request);
    if (result != COVER_IMAGE_OK) {
        return placement_of(result);
    }
    return cover_record_writer_finish(memory.writer) ? PLACEMENT_DONE : PLACEMENT_FAILED;
}

static int place(CoverState *state, const CoverJob *job, const ArtFile *art)
{
    uint32_t position = 0;
    uint32_t slot = 0;
    bool known = cover_index_find(&state->index, state->key, &position);
    if (!known && !cover_index_has_room(&state->index)) {
        return PLACEMENT_NO_ROOM;
    }
    if (!cover_index_free_slot(&state->index, &slot)) {
        return known ? PLACEMENT_DEFERRED : PLACEMENT_NO_ROOM;
    }
    Placement placement = decode_into(state, job, art, slot);
    state->attempts += placement == PLACEMENT_DONE || placement == PLACEMENT_UNDECODABLE ? 1u : 0u;
    if (placement != PLACEMENT_DONE) {
        return (int)placement;
    }
    return index_cover(state, position, known, slot, art->stamp) ? PLACEMENT_DONE
                                                                 : PLACEMENT_FAILED;
}

int cover_play_place(void *state, const CoverJob *job, const void *art)
{
    return place(state, job, art);
}
