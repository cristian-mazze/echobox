#include "cover_png.h"

#include <string.h>

static uint8_t *put_be32(uint8_t *at, uint32_t value)
{
    at[0] = (uint8_t)(value >> 24);
    at[1] = (uint8_t)(value >> 16);
    at[2] = (uint8_t)(value >> 8);
    at[3] = (uint8_t)value;
    return at + 4;
}

static uint8_t *put_chunk_start(uint8_t *at, uint32_t length, const char *type)
{
    at = put_be32(at, length);
    memcpy(at, type, 4);
    return at + 4;
}

static uint32_t adler32(const uint8_t *bytes, uint32_t length)
{
    uint32_t low = 1;
    uint32_t high = 0;
    for (uint32_t index = 0; index < length; index++) {
        low = (low + bytes[index]) % 65521u;
        high = (high + low) % 65521u;
    }
    return high << 16 | low;
}

static void fill_rows(uint8_t *raw, Color color)
{
    for (uint32_t row = 0; row < COVER_PNG_SIDE; row++) {
        uint8_t *line = raw + row * COVER_PNG_ROW;
        line[0] = 0;
        for (uint32_t column = 0; column < COVER_PNG_SIDE; column++) {
            line[1 + column * 3] = color.red;
            line[2 + column * 3] = color.green;
            line[3 + column * 3] = color.blue;
        }
    }
}

uint32_t cover_png_make(uint8_t *png, Color color)
{
    static uint8_t raw[COVER_PNG_RAW];
    static const uint8_t SIGNATURE[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    static const uint8_t HEADER_TAIL[5] = {8, 2, 0, 0, 0};
    static const uint8_t STORED[7] = {0x78,
                                      0x01,
                                      0x01,
                                      COVER_PNG_RAW & 0xFF,
                                      COVER_PNG_RAW >> 8,
                                      (uint8_t)~COVER_PNG_RAW,
                                      (uint8_t)(~COVER_PNG_RAW >> 8)};
    fill_rows(raw, color);
    uint8_t *at = png;
    memcpy(at, SIGNATURE, sizeof SIGNATURE);
    at = put_chunk_start(at + sizeof SIGNATURE, 13, "IHDR");
    at = put_be32(put_be32(at, COVER_PNG_SIDE), COVER_PNG_SIDE);
    memcpy(at, HEADER_TAIL, sizeof HEADER_TAIL);
    at = put_be32(at + sizeof HEADER_TAIL, 0);
    at = put_chunk_start(at, sizeof STORED + COVER_PNG_RAW + 4, "IDAT");
    memcpy(at, STORED, sizeof STORED);
    memcpy(at + sizeof STORED, raw, COVER_PNG_RAW);
    at = put_be32(put_be32(at + sizeof STORED + COVER_PNG_RAW, adler32(raw, COVER_PNG_RAW)), 0);
    at = put_be32(put_chunk_start(at, 0, "IEND"), 0);
    return (uint32_t)(at - png);
}
