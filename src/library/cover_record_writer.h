#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "image/cover_row_sink.h"
#include "library/library_builder.h"

enum {
    RECORD_FULL_ROW_BYTES = 112 * 2,
    RECORD_HALF_ROW_BYTES = 56 * 2,
    RECORD_FULL_BYTES = 112 * RECORD_FULL_ROW_BYTES,
    RECORD_CHUNK_BYTES = 18 * RECORD_FULL_ROW_BYTES
};

typedef struct RecordStage {
    uint8_t bytes[RECORD_CHUNK_BYTES];
    uint32_t used;
    uint32_t written;
} RecordStage;

typedef struct CoverRecordWriter {
    ByteSink sink;
    uint32_t offset;
    bool (*may_write)(void *context);
    void *context;
    RecordStage full;
    RecordStage half;
    bool failed;
} CoverRecordWriter;

void cover_record_writer_start(CoverRecordWriter *writer, ByteSink sink, uint32_t offset);
void cover_record_writer_gate(CoverRecordWriter *writer, bool (*may_write)(void *context),
                              void *context);
CoverRowSink cover_record_writer_sink(CoverRecordWriter *writer);
bool cover_record_writer_finish(CoverRecordWriter *writer);
