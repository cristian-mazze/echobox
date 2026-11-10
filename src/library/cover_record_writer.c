#include "cover_record_writer.h"

#include <stddef.h>

void cover_record_writer_start(CoverRecordWriter *writer, ByteSink sink, uint32_t offset)
{
    writer->sink = sink;
    writer->offset = offset;
    writer->may_write = NULL;
    writer->context = NULL;
    writer->full = (RecordStage){.used = 0, .written = 0};
    writer->half = (RecordStage){.used = 0, .written = 0};
    writer->failed = false;
}

void cover_record_writer_gate(CoverRecordWriter *writer, bool (*may_write)(void *context),
                              void *context)
{
    writer->may_write = may_write;
    writer->context = context;
}

static bool flush(CoverRecordWriter *writer, RecordStage *stage, uint32_t base)
{
    if (stage->used == 0) {
        return true;
    }
    if (writer->failed || (writer->may_write != NULL && !writer->may_write(writer->context)) ||
        !writer->sink.write(writer->sink.context, base + stage->written, stage->bytes,
                            stage->used)) {
        writer->failed = true;
        return false;
    }
    stage->written += stage->used;
    stage->used = 0;
    return true;
}

static void pack_row(RecordStage *stage, const uint16_t *pixels, uint32_t count)
{
    uint8_t *out = stage->bytes + stage->used;
    for (uint32_t index = 0; index < count; index++) {
        out[2 * index] = (uint8_t)(pixels[index] & 0xFF);
        out[2 * index + 1] = (uint8_t)(pixels[index] >> 8);
    }
    stage->used += 2 * count;
}

static bool take_row(CoverRecordWriter *writer, RecordStage *stage, uint32_t base,
                     const uint16_t *pixels, uint32_t count)
{
    if (stage->used + 2 * count > RECORD_CHUNK_BYTES && !flush(writer, stage, base)) {
        return false;
    }
    pack_row(stage, pixels, count);
    return true;
}

static bool take_full(void *context, uint32_t y, const uint16_t *pixels)
{
    CoverRecordWriter *writer = context;
    (void)y;
    return take_row(writer, &writer->full, writer->offset, pixels, RECORD_FULL_ROW_BYTES / 2);
}

static bool take_half(void *context, uint32_t y, const uint16_t *pixels)
{
    CoverRecordWriter *writer = context;
    (void)y;
    return take_row(writer, &writer->half, writer->offset + RECORD_FULL_BYTES, pixels,
                    RECORD_HALF_ROW_BYTES / 2);
}

CoverRowSink cover_record_writer_sink(CoverRecordWriter *writer)
{
    return (CoverRowSink){take_full, take_half, writer};
}

bool cover_record_writer_finish(CoverRecordWriter *writer)
{
    bool full = flush(writer, &writer->full, writer->offset);
    bool half = flush(writer, &writer->half, writer->offset + RECORD_FULL_BYTES);
    return full && half && !writer->failed;
}
