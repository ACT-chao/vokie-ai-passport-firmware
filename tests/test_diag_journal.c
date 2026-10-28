#include "diag_journal.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define BYTES (3 * DIAG_SECTOR_BYTES)
static uint8_t flash[BYTES];
static unsigned reads, erases, writes, fail_write;
static bool fail_erase;

static bool read_flash(void *ctx, uint32_t offset, void *data, size_t bytes)
{
    (void)ctx;
    assert(offset + bytes <= BYTES);
    ++reads;
    memcpy(data, flash + offset, bytes);
    return true;
}
static bool write_flash(void *ctx, uint32_t offset, const void *data, size_t bytes)
{
    (void)ctx;
    assert(offset >= DIAG_SECTOR_BYTES && offset + bytes <= BYTES);
    const uint8_t *src = data;
    ++writes;
    size_t amount = writes == fail_write ? bytes / 2 : bytes;
    for (size_t i = 0; i < amount; ++i) {
        assert((flash[offset + i] & src[i]) == src[i]);
        flash[offset + i] &= src[i];
    }
    return writes != fail_write;
}
static bool erase_flash(void *ctx, uint32_t offset, size_t bytes)
{
    (void)ctx;
    assert(offset >= DIAG_SECTOR_BYTES && offset % DIAG_SECTOR_BYTES == 0);
    assert(bytes == DIAG_SECTOR_BYTES && offset + bytes <= BYTES);
    ++erases;
    if (fail_erase) {
        memset(flash + offset, 0xff, bytes / 2);
        return false;
    }
    memset(flash + offset, 0xff, bytes);
    return true;
}
static diag_io_t io = {NULL, read_flash, write_flash, erase_flash};

static void format(void)
{
    memset(flash, 0xff, sizeof(flash));
    diag_header_t h = {.magic = DIAG_HEADER_MAGIC, .version = DIAG_VERSION,
        .record_bytes = DIAG_RECORD_BYTES, .sector_bytes = DIAG_SECTOR_BYTES,
        .partition_bytes = BYTES};
    h.crc = diag_crc32(&h, 60);
    memcpy(flash, &h, sizeof(h));
    reads = erases = writes = fail_write = 0;
    fail_erase = false;
}

static unsigned valid_records(void)
{
    unsigned count = 0;
    for (unsigned i = DIAG_SECTOR_BYTES; i < BYTES; i += DIAG_RECORD_BYTES) {
        diag_record_t record;
        memcpy(&record, flash + i, sizeof(record));
        count += diag_record_valid(&record);
    }
    return count;
}

static void assert_audio_defers(diag_journal_t *journal, const diag_record_t *event)
{
    const unsigned r = reads, w = writes, e = erases;
    const uint32_t slot = journal->next_slot, sequence = journal->next_sequence;
    assert(diag_journal_append(journal, event, false) == DIAG_BUSY);
    assert(reads == r && writes == w && erases == e);
    assert(journal->next_slot == slot && journal->next_sequence == sequence);
}

int main(void)
{
    assert(diag_crc32("123456789", 9) == 0xcbf43926u);
    diag_journal_t journal;
    const diag_record_t event = {.event = 45, .boot_id = 22, .uptime_ms = 6000,
        .session = 11, .args = {1, 178, 0, 0}};
    format();
    assert(diag_journal_init(&journal, &io, BYTES) == DIAG_OK);
    // Even a blank sector must not be read/programmed during capture.
    assert_audio_defers(&journal, &event);
    assert(diag_journal_append(&journal, &event, true) == DIAG_OK);
    assert(valid_records() == 1 && journal.next_sequence == 2 && erases == 0);
    assert(diag_journal_init(&journal, &io, BYTES) == DIAG_OK);
    assert(journal.next_sequence == 2 && journal.next_slot == 1);
    assert_audio_defers(&journal, &event);

    // Losing power midway through either the data or final commit cannot
    // invent a valid event or destroy the previous committed record.
    for (unsigned step = 1; step <= 2; ++step) {
        format();
        assert(diag_journal_init(&journal, &io, BYTES) == DIAG_OK);
        assert(diag_journal_append(&journal, &event, true) == DIAG_OK);
        fail_write = writes + step;
        assert(diag_journal_append(&journal, &event, true) == DIAG_IO_ERROR);
        assert(valid_records() == 1);
        fail_write = 0;
        assert(diag_journal_init(&journal, &io, BYTES) == DIAG_OK);
        assert(diag_journal_append(&journal, &event, true) == DIAG_OK);
        assert(valid_records() == 2 && journal.next_sequence == 3 && erases == 0);
    }
    // Also recover if the very first event is torn after provisioning.
    format();
    assert(diag_journal_init(&journal, &io, BYTES) == DIAG_OK);
    fail_write = 1;
    assert(diag_journal_append(&journal, &event, true) == DIAG_IO_ERROR);
    fail_write = 0;
    assert(diag_journal_init(&journal, &io, BYTES) == DIAG_OK);
    assert(diag_journal_append(&journal, &event, true) == DIAG_OK);
    assert(valid_records() == 1);

    // Ring wrap reclaims only the oldest sector. Audio defers every I/O stage.
    format();
    assert(diag_journal_init(&journal, &io, BYTES) == DIAG_OK);
    for (unsigned i = 0; i < journal.slots; ++i)
        assert(diag_journal_append(&journal, &event, true) == DIAG_OK);
    uint8_t snapshot[BYTES];
    memcpy(snapshot, flash, BYTES);
    assert_audio_defers(&journal, &event);
    assert(memcmp(snapshot, flash, BYTES) == 0 && erases == 0);
    assert(diag_journal_append(&journal, &event, true) == DIAG_OK);
    assert(erases == 1 && valid_records() == 65);
    assert(memcmp(snapshot, flash, DIAG_SECTOR_BYTES) == 0);
    assert(memcmp(snapshot + 2 * DIAG_SECTOR_BYTES, flash + 2 * DIAG_SECTOR_BYTES, DIAG_SECTOR_BYTES) == 0);
    assert(diag_journal_init(&journal, &io, BYTES) == DIAG_OK);
    assert(journal.next_sequence == 130);

    // A power cut during an erase may leave its prefix blank and its tail
    // programmed. Recovery must inspect the whole sector, then reclaim it.
    format();
    assert(diag_journal_init(&journal, &io, BYTES) == DIAG_OK);
    for (unsigned i = 0; i < journal.slots; ++i)
        assert(diag_journal_append(&journal, &event, true) == DIAG_OK);
    fail_erase = true;
    assert(diag_journal_append(&journal, &event, true) == DIAG_IO_ERROR);
    fail_erase = false;
    assert(diag_journal_init(&journal, &io, BYTES) == DIAG_OK && journal.next_sequence == 129);
    assert_audio_defers(&journal, &event);
    assert(diag_journal_append(&journal, &event, true) == DIAG_OK);
    assert(valid_records() == 65);

    // CRC corruption is rejected; sequence ordering also survives uint32 wrap.
    format();
    assert(diag_journal_init(&journal, &io, BYTES) == DIAG_OK);
    journal.next_sequence = UINT32_MAX;
    assert(diag_journal_append(&journal, &event, true) == DIAG_OK);
    assert(diag_journal_append(&journal, &event, true) == DIAG_OK);
    assert(diag_journal_init(&journal, &io, BYTES) == DIAG_OK && journal.next_sequence == 1);
    flash[DIAG_SECTOR_BYTES + 24] ^= 1;
    assert(valid_records() == 1);

    // Foreign/corrupt headers are never automatically formatted or erased.
    memset(flash, 0x5a, sizeof(flash));
    erases = writes = 0;
    assert(diag_journal_init(&journal, &io, BYTES) == DIAG_BAD_FORMAT);
    assert(diag_journal_append(&journal, &event, true) == DIAG_BAD_FORMAT);
    assert(erases == 0 && writes == 0);
    puts("Persistent diagnostic journal power-loss/wrap safety: PASS");
    return 0;
}
