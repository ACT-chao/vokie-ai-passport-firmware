#include "ble_audio_mbuf.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static unsigned small_free, large_free, frees, appends;
static bool short_buffer, append_failure;
static struct os_mbuf packet;

struct os_mbuf *os_msys_get_pkthdr(uint16_t size, uint16_t user_header)
{
    assert(user_header == 0);
    // Reproduce MSYS best-fit selection, with no fallback on pool exhaustion.
    unsigned pool = size + 8u <= 232u ? 1 : 2;
    unsigned *available = pool == 1 ? &small_free : &large_free;
    if (!*available) return NULL;
    --*available;
    packet = (struct os_mbuf){.pool = pool};
    packet.om_data = packet.storage + 8;
    packet.end = packet.storage + (short_buffer ? 180 : pool == 1 ? 232 : 296);
    return &packet;
}
int os_mbuf_append(struct os_mbuf *om, const void *data, uint16_t size)
{
    ++appends;
    if (append_failure || OS_MBUF_TRAILINGSPACE(om) < size) return -1;
    memcpy(om->om_data + om->om_len, data, size);
    om->om_len += size;
    return 0;
}
int os_mbuf_free_chain(struct os_mbuf *om)
{
    ++frees;
    if (om->pool == 1) ++small_free;
    else ++large_free;
    return 0;
}

int main(void)
{
    uint8_t audio[182];
    for (unsigned i = 0; i < sizeof(audio); ++i) audio[i] = i;
    // Field reproduction: ENOMEM with 24 free blocks because the small pool is empty.
    small_free = 0; large_free = 24;
    assert(os_msys_get_pkthdr(0, 0) == NULL);
    struct os_mbuf *om = ble_audio_mbuf_from_flat(audio, sizeof(audio));
    assert(om && om->pool == 2 && large_free == 23 && small_free == 0);
    assert(om->om_data - om->storage == 24 && om->om_len == sizeof(audio));
    assert(memcmp(om->om_data, audio, sizeof(audio)) == 0);
    assert(os_mbuf_free_chain(om) == 0 && large_free == 24);

    // Exhausted audio buffers must not consume the reserved control/RX pool.
    small_free = 12; large_free = 0;
    assert(ble_audio_mbuf_from_flat(audio, sizeof(audio)) == NULL && small_free == 12);
    om = os_msys_get_pkthdr(0, 0);
    assert(om && om->pool == 1);
    os_mbuf_free_chain(om);

    large_free = 24; short_buffer = true;
    unsigned prior = frees;
    assert(ble_audio_mbuf_from_flat(audio, sizeof(audio)) == NULL);
    assert(frees == prior + 1 && large_free == 24);
    short_buffer = false; append_failure = true; prior = frees;
    assert(ble_audio_mbuf_from_flat(audio, sizeof(audio)) == NULL);
    assert(frees == prior + 1 && large_free == 24);
    append_failure = false; prior = appends;
    assert(!ble_audio_mbuf_from_flat(NULL, 182));
    assert(!ble_audio_mbuf_from_flat(audio, 0));
    assert(!ble_audio_mbuf_from_flat(audio, 65535));
    assert(appends == prior);
    puts("BLE audio pool separation and ownership: PASS");
    return 0;
}
