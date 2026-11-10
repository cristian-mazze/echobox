#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/cover_state.h"

enum {
    COVER_QUEUE_CAPACITY = COVER_STATE_QUEUE
};

typedef struct CoverQueue {
    uint16_t albums[COVER_QUEUE_CAPACITY];
    uint32_t keys[COVER_QUEUE_CAPACITY];
    uint32_t count;
} CoverQueue;

void cover_queue_init(CoverQueue *queue);
bool cover_queue_push(CoverQueue *queue, uint16_t album, uint32_t key);
void cover_queue_remove(CoverQueue *queue, uint16_t album);
uint32_t cover_queue_order(const CoverQueue *queue, const uint16_t *current, uint16_t *order,
                           uint32_t capacity);
