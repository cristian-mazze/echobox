#include "cover_queue.h"

#include <stddef.h>

void cover_queue_init(CoverQueue *queue)
{
    queue->count = 0;
}

static uint32_t find(const CoverQueue *queue, uint16_t album)
{
    for (uint32_t index = 0; index < queue->count; index++) {
        if (queue->albums[index] == album) {
            return index;
        }
    }
    return queue->count;
}

static void close_gap(CoverQueue *queue, uint32_t gap)
{
    for (uint32_t index = gap; index + 1 < queue->count; index++) {
        queue->albums[index] = queue->albums[index + 1];
        queue->keys[index] = queue->keys[index + 1];
    }
    queue->count--;
}

void cover_queue_remove(CoverQueue *queue, uint16_t album)
{
    uint32_t found = find(queue, album);
    if (found < queue->count) {
        close_gap(queue, found);
    }
}

bool cover_queue_push(CoverQueue *queue, uint16_t album, uint32_t key)
{
    bool fresh = find(queue, album) == queue->count;
    cover_queue_remove(queue, album);
    if (queue->count == COVER_QUEUE_CAPACITY) {
        queue->count--;
    }
    for (uint32_t index = queue->count; index > 0; index--) {
        queue->albums[index] = queue->albums[index - 1];
        queue->keys[index] = queue->keys[index - 1];
    }
    queue->albums[0] = album;
    queue->keys[0] = key;
    queue->count++;
    return fresh;
}

uint32_t cover_queue_order(const CoverQueue *queue, const uint16_t *current, uint16_t *order,
                           uint32_t capacity)
{
    uint32_t count = 0;
    if (current != NULL && capacity > 0) {
        order[count++] = *current;
    }
    for (uint32_t index = 0; index < queue->count && count < capacity; index++) {
        if (current == NULL || queue->albums[index] != *current) {
            order[count++] = queue->albums[index];
        }
    }
    return count;
}
