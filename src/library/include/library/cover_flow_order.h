#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/library.h"

typedef struct CoverFlowOrder {
    uint32_t first_link;
    uint32_t count;
    bool linked;
} CoverFlowOrder;

CoverFlowOrder cover_flow_order_make(const Library *library);
bool cover_flow_order_album(const Library *library, CoverFlowOrder order, uint32_t position,
                            uint16_t *album);
bool cover_flow_order_find(const Library *library, CoverFlowOrder order, uint16_t album,
                           uint32_t *position);
