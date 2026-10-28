#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DIAG_HEADER_MAGIC UINT32_C(0x314a4456)
#define DIAG_RECORD_MAGIC UINT32_C(0x31524456)
#define DIAG_COMMIT UINT32_C(0x54494d43)
#define DIAG_VERSION 1
#define DIAG_RECORD_BYTES 64
#define DIAG_SECTOR_BYTES 4096
#define DIAG_PARTITION_BYTES 0x40000

typedef struct {
    uint32_t magic;
    uint16_t version, record_bytes;
    uint32_t sector_bytes, partition_bytes;
    uint32_t reserved[11];
    uint32_t crc;
} diag_header_t;

typedef struct {
    uint32_t magic;
    uint16_t version, event;
    uint32_t sequence, boot_id, uptime_ms, session;
    uint32_t args[8];
    uint32_t crc, commit;
} diag_record_t;

typedef struct {
    void *context;
    bool (*read)(void *, uint32_t, void *, size_t);
    bool (*write)(void *, uint32_t, const void *, size_t);
    bool (*erase)(void *, uint32_t, size_t);
} diag_io_t;

typedef struct {
    diag_io_t io;
    uint32_t slots, next_slot, next_sequence;
    bool initialized;
} diag_journal_t;

typedef enum { DIAG_OK, DIAG_BUSY, DIAG_BAD_FORMAT, DIAG_IO_ERROR } diag_result_t;

uint32_t diag_crc32(const void *data, size_t length);
bool diag_record_valid(const diag_record_t *record);
// An immutable format header is provisioned explicitly, never auto-formatted.
// Recovery skips torn records and never erases the header sector.
diag_result_t diag_journal_init(diag_journal_t *journal, const diag_io_t *io,
                                uint32_t partition_bytes);
// Single-writer context. allow_io=false performs no reads, writes or erases and
// returns DIAG_BUSY without advancing the cursor. Retry after audio pauses.
diag_result_t diag_journal_append(diag_journal_t *journal, const diag_record_t *event,
                                  bool allow_io);
