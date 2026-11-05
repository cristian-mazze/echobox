// This file is for reference only - showing correct style patterns
// In the ACTUAL .c/.h files: NO COMMENTS ALLOWED - names must carry intent

#ifndef EXAMPLE_H
#define EXAMPLE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t width;
    uint8_t height;
} Rect;

typedef enum {
    KEY_TYPE_PRESS,
    KEY_TYPE_LONG,
    KEY_TYPE_REPEAT
} KeyType;

typedef struct {
    KeyType type;
    uint32_t timestamp_ms;
} KeyEvent;

typedef struct {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
} Version;

#define MAX_PATH_LENGTH 255
#define KEY_CODE_MASK 0xFF
#define EVENT_QUEUE_SIZE 64

bool rect_contains_point(const Rect* rect, uint16_t x, uint16_t y);
uint16_t rect_width(const Rect* rect);
uint16_t rect_height(const Rect* rect);
void rect_fill(Rect* rect, uint16_t x, uint16_t y, uint8_t width, uint8_t height);

#endif