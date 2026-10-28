#include "diag_journal.h"

#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(diag_header_t) == 64, "journal header layout");
_Static_assert(sizeof(diag_record_t) == 64, "journal record layout");
_Static_assert(offsetof(diag_record_t, crc) == 56, "record CRC boundary");
_Static_assert(offsetof(diag_record_t, commit) == 60, "record commit boundary");

uint32_t diag_crc32(const void *data, size_t length)
{
    const uint8_t *bytes = data;
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (0u - (crc & 1u)));
    }
    return ~crc;
}

bool diag_record_valid(const diag_record_t *r)
{
    return r->magic == DIAG_RECORD_MAGIC && r->version == DIAG_VERSION &&
           r->commit == DIAG_COMMIT && r->crc == diag_crc32(r, offsetof(diag_record_t, crc));
}

static bool newer(uint32_t a, uint32_t b)
{
    const uint32_t delta = a - b;
    return delta != 0 && delta < UINT32_C(0x80000000);
}

static uint32_t address(uint32_t slot)
{
    return DIAG_SECTOR_BYTES + slot * DIAG_RECORD_BYTES;
}

static bool erased(const void *data, size_t length)
{
    const uint8_t *bytes = data;
    for (size_t i = 0; i < length; ++i) if (bytes[i] != 0xff) return false;
    return true;
}

diag_result_t diag_journal_init(diag_journal_t *j, const diag_io_t *io, uint32_t bytes)
{
    *j = (diag_journal_t){.io = *io};
    diag_header_t h;
    if (!io->read(io->context, 0, &h, sizeof(h))) return DIAG_IO_ERROR;
    if (bytes < 3 * DIAG_SECTOR_BYTES || bytes % DIAG_SECTOR_BYTES ||
        h.magic != DIAG_HEADER_MAGIC || h.version != DIAG_VERSION ||
        h.record_bytes != DIAG_RECORD_BYTES || h.sector_bytes != DIAG_SECTOR_BYTES ||
        h.partition_bytes != bytes || h.crc != diag_crc32(&h, offsetof(diag_header_t, crc)))
        return DIAG_BAD_FORMAT;
    j->slots = (bytes - DIAG_SECTOR_BYTES) / DIAG_RECORD_BYTES;
    bool found = false;
    uint32_t latest = 0, latest_slot = 0;
    for (uint32_t slot = 0; slot < j->slots; ++slot) {
        diag_record_t record;
        if (!io->read(io->context, address(slot), &record, sizeof(record))) return DIAG_IO_ERROR;
        if (diag_record_valid(&record) && (!found || newer(record.sequence, latest))) {
            found = true;
            latest = record.sequence;
            latest_slot = slot;
        }
    }
    j->next_sequence = found ? latest + 1 : 1;
    j->next_slot = found ? (latest_slot + 1) % j->slots : 0;
    j->initialized = true;
    return DIAG_OK;
}

diag_result_t diag_journal_append(diag_journal_t *j, const diag_record_t *event, bool allow_io)
{
    if (!j->initialized) return DIAG_BAD_FORMAT;
    if (!allow_io) return DIAG_BUSY;
    diag_record_t old;
    // Skip a torn write within the current sector. Reclaim only at a sector
    // boundary so a partial tail cannot erase newer committed neighbours.
    for (uint32_t scanned = 0; scanned < j->slots; ++scanned) {
        const uint32_t offset = address(j->next_slot);
        if (offset % DIAG_SECTOR_BYTES == 0) {
            bool blank = true;
            for (uint32_t cursor = 0; cursor < DIAG_SECTOR_BYTES; cursor += sizeof(old)) {
                if (!j->io.read(j->io.context, offset + cursor, &old, sizeof(old))) return DIAG_IO_ERROR;
                if (!erased(&old, sizeof(old))) { blank = false; break; }
            }
            if (!blank) {
                if (!j->io.erase(j->io.context, offset, DIAG_SECTOR_BYTES)) return DIAG_IO_ERROR;
            }
            break;
        }
        if (!j->io.read(j->io.context, offset, &old, sizeof(old))) return DIAG_IO_ERROR;
        if (erased(&old, sizeof(old))) break;
        j->next_slot = (j->next_slot + 1) % j->slots;
    }
    diag_record_t record = *event;
    record.magic = DIAG_RECORD_MAGIC;
    record.version = DIAG_VERSION;
    record.sequence = j->next_sequence;
    record.crc = diag_crc32(&record, offsetof(diag_record_t, crc));
    record.commit = DIAG_COMMIT;
    const uint32_t offset = address(j->next_slot);
    if (!j->io.write(j->io.context, offset, &record, offsetof(diag_record_t, commit)) ||
        !j->io.write(j->io.context, offset + offsetof(diag_record_t, commit), &record.commit, sizeof(record.commit)))
        return DIAG_IO_ERROR;
    ++j->next_sequence;
    j->next_slot = (j->next_slot + 1) % j->slots;
    return DIAG_OK;
}
